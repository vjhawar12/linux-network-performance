import psutil
from pathlib import Path
import subprocess
import os
import argparse
import json
import time
from pyroute2 import IPRoute
from pyroute2.netns import create as ns_create
from pyroute2.netns import setns as ns_setns
from pyroute2.ethtool import Ethtool
import threading

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
    threads = []
    # default namespace is indicated by passing namespace_str = None
    def __init__(self, intf_name, ip, namespace_str, stat_list):
        self.intf_name = intf_name
        self.ip = ip
        self.namespace_str = namespace_str   
        self.telemetry = dict()
        self.stop_collection = threading.Event()
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
        stat_list = set(stat_list)
        for item in stat_list:
            if not isinstance(item, str):
                raise NetworkInterfaceException(f"Invalid Key '{item}' for telemetry dict!") 
            self.telemetry[item] = 0
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
        addrs = psutil.net_if_addrs().get(self.intf_name)
        for addr in addrs:
            if addr is not None and addr.family.name == "AF_LINK":
                self.mac_address = addr.address
        if self.mac_address is None:
            raise NetworkInterfaceException(f"Could not find MAC address for interface '{self.intf_name}' number {self.intf_idx}")
        # scrape /sys/class/net for virtual files about each interface
        driver_dir = Path("/sys/class/net") / self.intf_name / "device" / "driver"
        actual_path = os.path.realpath(driver_dir)
        # printing out driver and mac address per interface
        self.driver_name = os.path.basename(actual_path)
        NetworkInterface.threads.append(threading.Thread(target=self))
                
    # returns the interface index and the namespace in which its defined
    def get_intf_index(self):
        return (self.namespace_str, self.intf_idx)

    @staticmethod
    def elapsed_ms(last_time, duration_ms):
        return ((1000 * time.perf_counter()) - last_time) > duration_ms

    def __call__(self):
        # performs the system call netns which shifts the entire os namespace 
        # and invalidates ipr if called within the context manager
        if self.namespace_str is not None:
            ns_setns(self.namespace_str) 
        self.ethtool = Ethtool()
        with IPRoute() as ipr:
            while not self.stop_collection.is_set():  
                print("Checking packet drop statistics:")
                self._collect(ipr)
                time.sleep(0.01)
                
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
        for intf_name, addresses in wired_interfaces.items():
            print(f"Interface: {intf_name}")


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
        if NetworkInterface.elapsed_ms(self.last_time_link_checked, 200):
            self.last_time_link_checked = 1000 * time.perf_counter()
            link_status = attrs.get('IFLA_OPERSTATE')
            self.link["up"] = link_status
            link = self.ethtool.get_link_mode(self.intf_name)
            self.link["speed"] = link.speed
            self.link["duplex"] = link.duplex
            self.link["autoneg"] = link.autoneg

    @staticmethod
    # call this only once all the instances of NetworkInterface have been added
    def start():
        for thread in NetworkInterface.threads:
            thread.start()

    @staticmethod
    # call this at the end of the program
    def join():
        for thread in NetworkInterface.threads:
            thread.join()        

    def snapshot(self):
        with self.snapshot_lock:
            return self.telemetry.copy()
           
    def __str__(self):
        s1 = f"Status: {self.link['up']}"
        s2 = f" Speed: {self.link['speed']} Mb/s"
        s3 = f" Driver: {self.driver_name}"
        s4 = f" MAC: {self.mac_address}"
        s5 = str(self.telemetry)
        return s1 + s2 + s3 + s4 + s5
       
def generate_traffic(rx, tx, bitrate, duration):
    # 2. Run iperf3 to saturate the link (using UDP to force queue pressure)
    print(f"Saturating link to {rx.ip} via {tx.intf_name}...")
    cmd = ["iperf3", "-c", rx.ip, "-B", tx.ip, "--bind-dev", tx.intf_name, "-u", "-b", bitrate, "-t", str(duration), "-J"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    report = json.loads(result.stdout)
    summary = report["end"]["sum"]
    bits_ps = summary["bits_per_second"]
    packets_lost = summary["lost_packets"]
    packets_sent = summary["packets"]
    loss_percent = summary["lost_percent"]
    jitter_ms = summary["jitter_ms"]
    if "error" in report:
        raise RuntimeError(report["error"])
    return summary

def run_test(rx, tx, bitrate, duration):
    def calculate_deltas(initial, final):
        delta = {}
        for key in initial:
            delta[key] = final[key][1] - initial[key][1]
        return delta
    # 1. Snapshot L2 stats before the stress test
    ms_start = int(time.time() * 1000)
    initial_l2_rx = rx.snapshot()
    initial_l2_tx = tx.snapshot()
    # 2. generate traffic
    summary = generate_traffic(rx, tx, bitrate, duration)
    rx.stop_collection.set()
    tx.stop_collection.set()
    # 3. Snapshot L2 stats immediately after the test
    final_l2_rx = rx.snapshot()
    final_l2_tx = tx.snapshot()
    ms_end = int(time.time() * 1000)
    # 4. Calculate deltas
    delta_rx = calculate_deltas(initial_l2_rx, final_l2_rx)
    delta_tx = calculate_deltas(initial_l2_tx, final_l2_tx)
    time_diff_ms = ms_end - ms_start 
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
    print(f"Bits per second: {summary["bits_per_second"]}")
    print(f"Loss percent: {summary["lost_percent"]}")
    print(f"Jitter ms: {summary["jitter_ms"]}")

def app(args):
    # print out all the interfaces found on the machine
    NetworkInterface.discover_interfaces()
    # create the tx and rx interfaces from command line arguments
    tx = NetworkInterface(args.tx_interface, args.tx_ip, None, NetworkInterface.STAT_NAMES["TX"])
    rx = NetworkInterface(args.rx_interface, args.rx_ip, args.rx_namespace, NetworkInterface.STAT_NAMES["RX"])
    # calling start to launch the per-interface threads
    NetworkInterface.start()
    # using iperf3 to have the server that tracks the packets the client sent and checks how many actually made it to server
    # runs the command in the rxbench namespace specifically
    # sudo ip netns exec rxbench iperf3 -s -B 172.31.250.2
    cmd = subprocess.Popen([
        "ip", "netns", "exec", rx.namespace_str, 
        "iperf3", "-s", "-1", "-B", rx.ip
        ], 
        stdout=subprocess.DEVNULL, 
        stderr=subprocess.PIPE, 
        text=True
    )
    # running the iperf3 test to measure packet drop
    run_test(rx, tx, args.bitrate, args.duration)

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
    app(args)
    # calling join at the end of the program for cleanup
    NetworkInterface.join()