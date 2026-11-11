import psutil
from pathlib import Path
import subprocess
import os
import argparse
import json
import time
from pyroute2 import IPRoute, RTMGRP_LINK
from pyroute2.netns import create as ns_create
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
    processes = []
    # default namespace is indicated by passing namespace_str = None
    def __init__(self, intf_name, ip, namespace_str, stat_list):
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
        self._cached_data = dict()
        stat_list = set(stat_list)
        for item in stat_list:
            if not isinstance(item, str):
                raise NetworkInterfaceException(f"Invalid Key '{item}' for telemetry dict!") 
            self.telemetry[item] = (time.time(), 0)
        addrs = psutil.net_if_addrs().get(self.intf_name)
        for addr in addrs:
            if addr is not None and addr.family.name == "AF_LINK":
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
            ns_create(self.namespace_str)         
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
        with (IPRoute(netns=self.namespace_str) if self.namespace_str else IPRoute()) as ipr:    
            while not NetworkInterfaceProcess.stop_collection.is_set():  
                self._collect(ipr)
                # at >=10G rates polling at 1s for packet drops can be too slow to accurately capture the time at which packets were dropped
                time.sleep(0.01) 

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
        stats = attrs.get('IFLA_STATS64')
        with self.snapshot_lock:
            for stat in stats.keys():
                if stat in self.telemetry:
                    self.telemetry[stat] = (time.time(), stats.get(stat, 0))        

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
    
class NetworkInterfaceProcess:
    processes = []
    stop_collection = Event()
    
    def __init__(self, intf_name, ip, namespace, stat_list, conn):
        self.intf_name = intf_name
        self.ip = ip
        self.namespace = namespace
        self.stat_list = stat_list
        self.conn = conn
        NetworkInterfaceProcess.stop_collection.clear()
        NetworkInterfaceProcess.processes.append(
            Process(
                    target=NetworkInterfaceProcess.stream,
                    args=(intf_name, ip, namespace, stat_list, conn)
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
    def stream(intf_name, ip, namespace, stat_list, conn):
        intf = NetworkInterface(intf_name, ip, namespace, stat_list)
        while not NetworkInterfaceProcess.stop_collection.is_set():        
            conn.send(intf.snapshot())
            time.sleep(0.01)

async def generate_traffic_async(rx, tx, bitrate, duration):
    cmd = ["iperf3", "-c", rx.ip, "-B", tx.ip, "--bind-dev", tx.intf_name, "-u", "-b", bitrate, "-t", str(duration), "-J"]
    process = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT
    )
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
        # recv is blocking so we want to check if there's available data
        # before waiting for it to arrive via stream
        if conn.poll():
            # collect and push the data to the buffer
            data, timestamp_ms = await asyncio.to_thread(conn.recv)
            buffer.append((data, timestamp_ms))
        # every 1s publish the data collected so its responsive without 
        # too much overhead of file I/O
        if time.monotonic_ns() / 1E6 - _timestamp_ms > 1000:
            length = len(buffer)
            publish(buffer[length:], fname)
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
    await asyncio.sleep(0.01)
    # Snapshot L2 stats before the stress test using pipes to pass data
    initial_l2_rx, _ =  buffer_rx[-1]
    initial_l2_tx, start_ms_tx = buffer_tx[-1]
    # generate traffic
    summary = await generate_traffic_async(rx, tx, bitrate, duration)
    NetworkInterfaceProcess.stop_collection.set()
    # snapshot L2 stats immediately after the test
    final_l2_rx, _ = buffer_rx[-1]
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
    asyncio.create_task(monitor(tx_conn_2, buffer_tx, "tx.txt"))
    asyncio.create_task(monitor(rx_conn_2, buffer_rx, "rx.txt"))
    # print out all the interfaces found on the machine
    NetworkInterface.discover_interfaces()
    # create the tx and rx interfaces from command line arguments
    tx = NetworkInterfaceProcess(args.tx_interface, args.tx_ip, None, NetworkInterface.STAT_NAMES["TX"], tx_conn_1)
    rx = NetworkInterfaceProcess(args.rx_interface, args.rx_ip, args.rx_namespace, NetworkInterface.STAT_NAMES["RX"], rx_conn_1)
    # calling start to launch the per-interface threads
    NetworkInterfaceProcess.startall()
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

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tx_interface")
    parser.add_argument("rx_interface")
    parser.add_argument("--rx-ip", default="172.31.250.2")
    parser.add_argument("--tx-ip", default="172.31.250.1")
    parser.add_argument("--rx-namespace", default="rxbench")
    parser.add_argument("--bitrate", default="1G")
    parser.add_argument("--duration", type=int, default=10)
    args = parser.parse_args()
    # create pipes for process to process communication
    tx_conn_1, tx_conn_2 = Pipe()
    rx_conn_1, rx_conn_2 = Pipe()
    # create buffers to store snapshots of data 
    buffer_tx = []
    buffer_rx = []
    # viewing statistics in real time
    asyncio.run(app(tx_conn_1, rx_conn_1,  tx_conn_2, rx_conn_2, buffer_tx, buffer_rx, args))
    # calling join at the end of the program for cleanup
    NetworkInterfaceProcess.joinall()