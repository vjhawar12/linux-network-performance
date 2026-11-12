import psutil
from pathlib import Path
import subprocess
import os
import argparse
import json
import time
from pyroute2 import IPRoute, RTMGRP_LINK, netns
import threading
from multiprocessing import Process, Event, Pipe
import asyncio

""" 
Sending packets from one network interface to another on the linux machine with the switch in between 
and inspecting packet drop statistics for Layer 2. The machine has 2 NICs which are being used as interface 1 and 2. 
NIC1 -> Source port on switch -> Destination port on switch -> NIC 2
iperf3 operates at Layer 4 and generates UDP/TCP packets to verify if each packet sent was received. 
Layer 2 packet drop statistics need to be examined by reading virtual files on the linux machine. 
Both interfaces need to be assigned ip addresses with one designated as 'client' / 'tx' and the other 'server' / 'rx'
The linux kernel routes packets going from one interface to another on the same machine internally not neccessarily 
over the switch as we intend to. Using network namespaces isolates interfaces to get around that by creating 
a virtual networking stack so that the routing table doesn't think the rx interface is on the same machine as the 
tx interface and so the kernel doesn't use the loopback interface to route the packets between tx and rx interfaces
"""

class NetworkInterfaceException(Exception):
    pass

# high performance timer tracks time with ns precision and maintains its own internal state so its 
# not subject to os jitter (the kernel scheduling a thread slightly late) affecting when it calculates the deadline
class HighPerformanceTimer:
    def __init__(self, wait_ms):
        if wait_ms <= 0:
            raise ValueError(f"wait_ms {wait_ms} must be > 0!")
        self.last_timestamp = time.monotonic_ns() / 1E6
        self.wait_ms = wait_ms
        self.deadline_ms = wait_ms + self.last_timestamp

    # computes the next deadline
    def tick(self):
        now = time.monotonic_ns() / 1E6
        if now >= self.deadline_ms:
            times = 1 + ((now - self.deadline_ms) // self.wait_ms)
            self.deadline_ms += times * self.wait_ms

    # returns True if the last-set deadline was exceeded 
    def elapsed(self):
        now = time.monotonic_ns() / 1E6
        if  now >= self.deadline_ms:
            self.last_timestamp = now
            return True
        return False

    # returns the difference between the current nanosecond and the deadline
    # will return 0 if deadline was exceeded
    def remaining_ms(self):
        now = time.monotonic_ns() / 1E6
        return max(0, self.deadline_ms - now)

class NetworkInterface:
    STAT_NAMES = {
        "TX": (
            "tx_packets",
            "tx_bytes",
            "tx_dropped",
            "tx_errors",
        ),
        "RX" : (
            "rx_packets",
            "rx_bytes",
            "rx_dropped",
            "rx_errors",
            "rx_crc_errors",
            "rx_missed_errors",
        )
    }
    # default namespace is indicated by passing namespace_str = None
    def __init__(self, intf_name, ip, namespace_str, stat_list, namespace_created):
        self.intf_name = intf_name
        self.ip = ip
        self.namespace_str = namespace_str   
        self.telemetry = dict()
        self.snapshot_lock = threading.Lock()
        self.last_time_link_checked = 0
        self.intf_idx = None
        self.mac_address = None
        self.driver_name = None
        self.link = {
            "up" : None,
            "speed" : None,
            "duplex" : None,
            "autoneg" : None,
        }
        self.threads = []
        self.namespace_created = namespace_created
        self._cached_data = dict()
        stat_list = set(stat_list)
        for item in stat_list:
            if not isinstance(item, str):
                raise NetworkInterfaceException(f"Invalid Key '{item}' for telemetry dict!") 
            self.telemetry[item] = (time.time(), 0)
        addrs = psutil.net_if_addrs().get(self.intf_name)
        for addr in addrs:
            if addr is not None and addr.family.name == "AF_PACKET":
                self.mac_address = addr.address
        if self.mac_address is None:
            raise NetworkInterfaceException(f"Could not find MAC address for interface '{self.intf_name}'")
        # scrape /sys/class/net for virtual files about each interface
        driver_dir = Path("/sys/class/net") / self.intf_name / "device" / "driver"
        actual_path = os.path.realpath(driver_dir)
        # printing out driver and mac address per interface
        self.driver_name = os.path.basename(actual_path)
        # adding the interface to the network namespace if its a custom namespace
        if self.namespace_str is not None:
            # creating the network namespace if a custom namespace is specified
            netns.create(self.namespace_str)         
            self.namespace_created.set()
            with IPRoute() as ipr:
                # converting names of interfaces into numeric ids
                indices = ipr.link_lookup(ifname=self.intf_name)
                if not indices:
                    raise NetworkInterfaceException(f"Interface '{self.intf_name}' not found at startup.")
                self.intf_idx = indices[0]     
                # adding the interface to the network namespace if its a custom namespace
                ipr.link("set", index=self.intf_idx, net_ns_fd=self.namespace_str)
        with (IPRoute(netns=self.namespace_str) if self.namespace_str else IPRoute()) as ipr:    
            # converting names of interfaces into numeric ids
            indices = ipr.link_lookup(ifname=self.intf_name)
            if not indices:
                raise NetworkInterfaceException(f"Interface '{self.intf_name}' not found at startup.")
            self.intf_idx = indices[0]     
            # assign the static IP
            ipr.addr("add", index=self.intf_idx, address=self.ip, prefixlen=24)
            # bring the interface up
            ipr.link("set", index=self.intf_idx, state="up")
        # spawning 2 threads per instance/interface, one to poll packet drop counters and another to listen for link status changes
        self.threads.append(threading.Thread(target=self.poll_counters))
        self.threads.append(threading.Thread(target=self.listen_link_status))
        for thread in self.threads:
            thread.start()
                
    # returns the interface index and the namespace in which its defined
    def get_intf_index(self):
        return (self.namespace_str, self.intf_idx)

    # polling for packet drop statistics because kernel does not send multicast messages just because a counter was incremented
    def poll_counters(self):
        # polling at 10ms
        timer = HighPerformanceTimer(wait_ms=10)
        with (IPRoute(netns=self.namespace_str) if self.namespace_str else IPRoute()) as ipr:    
            while not NetworkInterfaceProcess.stop_collection.is_set():  
                self._collect(ipr)
                NetworkInterfaceProcess.stop_collection.wait(timer.remaining_ms() / 1000)
                timer.tick()

    # eventually we want to implement link status down => stop polling for counters, for now we just capture link status and report it
    def listen_link_status(self):
        # event driven architecture to have thread update link status only when kernel sends a multicast message 
        # with a link status event, therefore avoiding unneccessary polling of link status
        with (IPRoute(netns=self.namespace_str) if self.namespace_str else IPRoute()) as ipr:
            # socket is listening for link messages specifically rather than everything
            ipr.bind(RTMGRP_LINK)
            # have the socket wake up every 5s so that its not sleeping for a long time during when stop_collection was set
            ipr.nl_socket.settimeout(5.0)  
            # waits on the same Event as poll_counters, can seperate the two later if needed
            while not NetworkInterfaceProcess.stop_collection.is_set():
                try:
                    # get is a blocking method that returns netlink messages via kernel system calls
                    # it might throw timeout error if no message received because of settimeout from above, verify that though
                    msg = ipr.get() 
                    self._cached_data = msg[0]
                    # filtering by interface index to avoid parsing the wrong one
                    if self._cached_data['index'] != self.intf_idx:
                        continue
                    for attrs in self._cached_data['attrs']:
                        if attrs[0] == 'IFLA_OPERSTATE':
                            self.link["up"] = attrs[1]
                except TimeoutError:
                    pass
                
    @staticmethod
    def discover_interfaces(wired_prefixes=('eth', 'en', 'em', 'br', 'lan'), wireless_prefixes=('wlan', 'wlp', 'wifi', 'wlan')):
        # Get all network interface addresses using psutil which tracks processes including network processes
        interfaces = psutil.net_if_addrs()
        # casting to set for unique values only
        wired_prefixes = tuple(set(wired_prefixes))
        wireless_prefixes = tuple(set(wireless_prefixes))
        # Filter for interfaces that match wired prefixes and ignore wireless/loopback
        wired_interfaces = {
            name: addrs 
            for name, addrs in interfaces.items()
            if name.lower().startswith(wired_prefixes) 
            and not name.lower().startswith(wireless_prefixes)
            and name != 'lo'
        }
        print("Available Network Interfaces:\n")
        for intf_name, address in wired_interfaces.items():
            print(f"Interface name: {intf_name} Address: {address}")

    # call this regularly preferably in its own thread to track packet drop and errors
    # inspect statistics like packet drop and throughput for a network interface 
    # for interfaces within the default namespace reading the contents of the virtual file 
    # /sys/class/net/<interface>/statistics is fine, but interfaces in alternate namespaces wouldn't show up there
    # so this function uses pyroute2/netlink to fetch those same statistics, 
    # but not by reading a virtual file directly 
    def _collect(self, ipr):
        if ipr is None:
            raise NetworkInterfaceException("No instance of IPRoute passed!")
        # Request specific link info including binary attributes
        link_data = ipr.get_links(self.intf_idx)[0]
        # Convert attributes tuple list into a workable dictionary
        attrs = link_data['attrs']        
        # 'IFLA_STATS64' contains the exact mirror of the /sys/../statistics files and returns the 64 bit version
        stats = None
        for attr in attrs:
            if attr[0] == "IFLA_STATS64":
                stats = attr[1]
                break
        if stats is not None:
            with self.snapshot_lock:
                for stat in stats.keys():
                    if stat in self.telemetry:
                        self.telemetry[stat] = (time.time(), stats.get(stat, 0))
        else:
            raise NetworkInterfaceException("Could not find IFLA_STATS64")        

    def snapshot(self):
        with self.snapshot_lock:
            # reporting a snapshot of the telemetry dict 
            # and the exact timestamp in ms when it was taken
            return self.telemetry.copy(), time.monotonic_ns() / 1E6
           
    def __str__(self):
        s1 = f"Status: {self.link['up']}"
        s2 = f" Speed: {self.link['speed']} Mb/s"
        s3 = f" Driver: {self.driver_name}"
        s4 = f" MAC: {self.mac_address}"
        s5 = str(self.snapshot())
        return s1 + s2 + s3 + s4 + s5

"""
why multiprocessing 

trying to isolate each interface because if a link for one interface goes down, or if one interface stalls
due to vibration causing the physical link to go down for a bit we dont want to stop tracking packets 
for the other interfaces, nor do we want to miss our 10ms deadline

multiprocessing specifically was used because we're doing a lot of writing, even when collecting statistics every 10ms and writing every 1s 
that's a lot of data being written frequently which can cause the GIL to be held for a longer time and can miss the 10ms deadline
by quite a bit. combine that with garbage collection, and heavy packet generation (done on same host) and 10ms could be missed frequently 
and by a large margin.

a 10ms deadline is used because it lets us accurately track packet drop statistics for 10G+ links in real time and how vibration affects the drop count
for automotive/aerospace applications vibration can happen at very high frequencies causing the physical connector to oscillate very quickly, 
so a slower deadline would be too coarse and could smooth out the data not providing enough granularity into vibration analysis during sine-sweep tests
on the shaker table

using a single process for multiple interfaces with seperate network namespaces would trigger namespace switching which is tricky to manage
and could get skipped during a netlink error, causing a socket to potentially be opened in the wrong namespace. seperate processes solve this because
that process has a single namespace so there's no switching

core pinning is another reason why multiprocessing was chosen, at 40/100G speeds to fully saturate the link, the cpu core has to generate a lot of packets
and that can overwhelm it, so we'd want to pin certain cores for packet generation exclusively which would require managing multiple processes
"""

class NetworkInterfaceProcess:
    processes = []
    stop_collection = Event()
    
    def __init__(self, intf_name, ip, namespace, stat_list, conn):
        self.intf_name = intf_name
        self.ip = ip
        self.namespace = namespace
        self.stat_list = stat_list
        self.conn = conn
        self.namespace_created = Event()
        self.stream_started = Event()
        self.stream_finished = Event()
        NetworkInterfaceProcess.stop_collection.clear()
        NetworkInterfaceProcess.processes.append(
            Process(
                    target=NetworkInterfaceProcess.stream,
                    args=(intf_name, ip, 
                          namespace, 
                          stat_list, conn, 
                          self.namespace_created, 
                          self.stream_started,
                          self.stream_finished
                    )
            )
        )

    @staticmethod
    def startall():
        for process in NetworkInterfaceProcess.processes:
            process.start()

    @staticmethod
    def joinall():
        for process in NetworkInterfaceProcess.processes:
            process.join()

    # use a pipe to stream the snapshot of the interface process 
    # with static we avoid having to send all the data associated with
    # the self object to the other process 
    @staticmethod
    def stream(intf_name, ip, namespace, stat_list, conn, namespace_created, stream_started, stream_finished):
        intf = NetworkInterface(intf_name, ip, namespace, stat_list, namespace_created)
        timer = HighPerformanceTimer(wait_ms=10)
        _set = False
        while not NetworkInterfaceProcess.stop_collection.is_set():    
            if not _set:
                stream_started.set()
                _set = True  
            conn.send(intf.snapshot())
            NetworkInterfaceProcess.stop_collection.wait(timer.remaining_ms() / 1000)
            timer.tick()
        stream_finished.set()  

async def generate_traffic_async(rx, tx, bitrate, duration):
    cmd = ["iperf3", "-c", rx.ip, "-B", tx.ip, "--bind-dev", tx.intf_name, "-u", "-b", bitrate, "-t", str(duration), "-J"]
    # creates a subprocess managed by the os
    process = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT
    )
    # waits for the subprocess to finish
    data, err = await process.communicate()
    if err:
        print(f"Kernel/Process Error Captured: {err.decode('utf-8')}")
    report = json.loads(data.decode('utf-8'))
    summary = report["end"]["sum"]
    return summary

# continuous real-time monitoring of packet statistics
# this method might return later than when the timestamp was actually taken
# but it returns the earlier timestamp which is correctly associated with the data
# scheduled with asyncio.create_task
async def monitor(conn, buffer, fname):
    _timestamp_ms = time.monotonic_ns() / 1E6
    length = 0
    while not NetworkInterfaceProcess.stop_collection.is_set():
        # recv is blocking so we want to drain the pipe 
        while conn.poll():
            # collect and push the data to the buffer
            data, timestamp_ms = await asyncio.to_thread(conn.recv)
            buffer.append((data, timestamp_ms))
        # every 1s publish the data collected so its responsive without 
        # too much overhead of file I/O. don't require high performance timer here
        # since this isn't a strict deadline
        if time.monotonic_ns() / 1E6 - _timestamp_ms > 1000:
            _timestamp_ms = time.monotonic_ns() / 1E6
            publish(buffer[length:], fname)
            length = len(buffer)
        # sleep for 5 ms to avoid 100% cpu spin while catching the data arriving every 10ms
        await asyncio.sleep(0.005)
    # publish one last time to include the last chunk of data
    publish(buffer[length:], fname)

def publish(buffer, fname):
    with open(fname, 'a') as f:
        for line in buffer:
            f.write(f"Timestamp ms: {line[1]} Data: {line[0]}\n")

async def run_test(rx, tx, bitrate, duration, buffer_tx, buffer_rx):
    def calculate_deltas(initial, final):
        delta = {}
        for key in initial:
            delta[key] = final[key][1] - initial[key][1]
        return delta
    # pausing to let the first element enter the buffers 
    while not buffer_rx or not buffer_tx:
        await asyncio.sleep(0.01)
    # Snapshot L2 stats before the stress test using pipes to pass data
    rx.stream_started.wait()
    initial_l2_rx, _ =  buffer_rx[-1]
    tx.stream_started.wait()
    initial_l2_tx, start_ms_tx = buffer_tx[-1]
    # generate traffic
    summary = await generate_traffic_async(rx, tx, bitrate, duration)
    NetworkInterfaceProcess.stop_collection.set()
    # snapshot L2 stats immediately after the test
    rx.stream_finished.wait()
    final_l2_rx, _ = buffer_rx[-1]
    tx.stream_finished.wait()
    final_l2_tx, end_ms_tx = buffer_tx[-1]
    # calculate deltas
    delta_rx = calculate_deltas(initial_l2_rx, final_l2_rx)
    delta_tx = calculate_deltas(initial_l2_tx, final_l2_tx)
    time_diff_ms = end_ms_tx - start_ms_tx 
    print("\n=== NIC STATISTICS ===")
    print(f"RX packets: {delta_rx['rx_packets']}")
    print(f"RX bytes: {delta_rx['rx_bytes']}")
    print(f"RX drops: {delta_rx['rx_dropped']}")
    print(f"RX errors: {delta_rx['rx_errors']}")
    print(f"RX CRC errors: {delta_rx['rx_crc_errors']}")
    print(f"RX missed: {delta_rx['rx_missed_errors']}")
    print(f"TX packets: {delta_tx['tx_packets']}")
    print(f"TX bytes: {delta_tx['tx_bytes']}")
    print(f"TX drops: {delta_tx['tx_dropped']}")
    print(f"TX errors: {delta_tx['tx_errors']}")
    print("\n=== IPERF3 SUMMARY ===")
    print(f"Bits per second: {summary['bits_per_second']}")
    print(f"Loss percent: {summary['lost_percent']}")
    print(f"Jitter ms: {summary['jitter_ms']}")
    print(f"Time: {time_diff_ms}")

async def app(tx_conn_1, rx_conn_1, tx_conn_2, rx_conn_2, buffer_tx, buffer_rx, args):
    monitor_tx = asyncio.create_task(monitor(tx_conn_2, buffer_tx, args.tx_file))
    monitor_rx = asyncio.create_task(monitor(rx_conn_2, buffer_rx, args.rx_file))
    # print out all the interfaces found on the machine
    NetworkInterface.discover_interfaces()
    # create the tx and rx interfaces from command line arguments
    tx = NetworkInterfaceProcess(args.tx_interface, args.tx_ip, None, NetworkInterface.STAT_NAMES["TX"], tx_conn_1)
    rx = NetworkInterfaceProcess(args.rx_interface, args.rx_ip, args.rx_namespace, NetworkInterface.STAT_NAMES["RX"], rx_conn_1)
    # calling start to launch the per-interface threads
    NetworkInterfaceProcess.startall()
    # waiting for the network namespace to be created before running the command
    rx.namespace_created.wait()
    # using iperf3 to have the server that tracks the packets the client sent and checks how many actually made it to server
    # runs the command in the rxbench namespace specifically
    # sudo ip netns exec rxbench iperf3 -s -B 172.31.250.2
    cmd = subprocess.Popen([
        "ip", "netns", "exec", rx.namespace, 
        "iperf3", "-s", "-1", "-B", rx.ip
        ], 
        stdout=subprocess.DEVNULL, 
        stderr=subprocess.PIPE, 
        text=True
    )
    # running the iperf3 test to measure packet drop
    await run_test(rx, tx, args.bitrate, args.duration, buffer_tx, buffer_rx)
    # need to wait for monitor to finish before app returns and shutdown
    await asyncio.gather(monitor_tx, monitor_rx)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tx_interface")
    parser.add_argument("rx_interface")
    parser.add_argument("--rx-ip", default="172.31.250.2")
    parser.add_argument("--tx-ip", default="172.31.250.1")
    parser.add_argument("--rx-namespace", default="rxbench")
    parser.add_argument("--bitrate", default="1G")
    parser.add_argument("--duration", type=int, default=10)
    parser.add_argument("--tx_file", default="tx.txt")
    parser.add_argument("--rx_file", default="rx.txt")
    args = parser.parse_args()
    if args.rx_namespace is not None and args.rx_namespace in netns.listnetns():
        netns.remove(args.rx_namespace)
    # clearing existing file contents first
    with open(args.tx_file, "w") as f:
        pass
    with open(args.rx_file, "w") as f:
        pass
    # create pipes for process to process communication
    tx_conn_1, tx_conn_2 = Pipe()
    rx_conn_1, rx_conn_2 = Pipe()
    # create buffers to store snapshots of data 
    buffer_tx = []
    buffer_rx = []
    # viewing statistics in real time
    try:
        # creates the event loop for asyncio with the first coroutine as 'app'
        asyncio.run(app(tx_conn_1, rx_conn_1,  tx_conn_2, rx_conn_2, buffer_tx, buffer_rx, args))
        # calling join at the end of the program for cleanup
        NetworkInterfaceProcess.joinall()
    finally:
        if args.rx_namespace is not None and args.rx_namespace in netns.listnetns():
            netns.remove(args.rx_namespace)