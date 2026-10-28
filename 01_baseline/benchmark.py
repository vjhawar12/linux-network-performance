import psutil
from pathlib import Path
import subprocess
import os
import argparse
import json
import time
from pyroute2 import NetNS, IPRoute
from pyroute2.netns import create as ns_create
import threading

""" 
Sending packets from one network interface to another on the linux machine with the switch in between 
and inspecting packet drop statistics for Layer 2. The machine has 2 NICs which are being used as interface 1 and 2. 
NIC1 -> Source port on switch -> Destination port on switch -> NIC 2
iperf3 operates at Layer 4 and generates UDP/TCP packets to verify if each packet sent was received. 
Layer 2 packet drop statistics need to be examined by reading virtual files on the linux machine. 
Both interfaces need to be assigned ip addresses with one designated as 'client' / 'tx' and the other 'server' / 'rx'
The linux kernel routes packets going from one interface to another on the same machine internally not neccessarily 
over the switch as we intend to. Using network namespaces isolates interfaces to get around that.
"""

parser = argparse.ArgumentParser()
parser.add_argument("tx_interface")
parser.add_argument("rx_interface")
parser.add_argument("--rx-ip", default="172.31.250.2")
parser.add_argument("--tx-ip", default="172.31.250.1")
parser.add_argument("--rx-namespace", default="rxbench")
parser.add_argument("--bitrate", default="1G")
parser.add_argument("--duration", type=int, default=10)

args = parser.parse_args()
tx_interface = args.tx_interface
rx_interface = args.rx_interface
rx_ip = args.rx_ip
tx_ip = args.tx_ip
rx_namespace = args.rx_namespace
bitrate = args.bitrate
duration = args.duration

threads = []
stop_collection = threading.Event()
snapshot_lock = threading.Lock()

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

realtime_telem = {
    "RX" : {},
    "TX" : {}
}

def setup_network(rx_itf_idx, tx_itf_idx):
    global rx_ip, tx_ip
    ns_create(rx_namespace)
    with IPRoute() as ipr:
        # add the rx interface to the rx namespace
        ipr.link("set", index=rx_itf_idx, net_ns_fd=rx_namespace)
        # assign the static IP for rx and tx    
        ipr.addr("add", index=rx_itf_idx, address=rx_ip, prefixlen=24)
        ipr.addr("add", index=tx_itf_idx, address=tx_ip, prefixlen=24)
        # bring the interface up
        ipr.link("set", index=rx_itf_idx, state="up")


def get_interface_index(name, namespace=None):
    with IPRoute(netns=namespace) if namespace else IPRoute() as ipr:
        indices = ipr.link_lookup(ifname=name)
        if not indices:
            raise ValueError(f"Interface '{name}' not found at startup.")
        return indices[0]

# inspect statistics like packet drop and throughput for a network interface 
# for interfaces within the default namespace reading the contents of the virtual file 
# /sys/class/net/<interface>/statistics is fine, but interfaces in alternate namespaces wouldn't show up there
# so this function uses pyroute2/netlink to fetch those same statistics, 
# but not by reading a virtual file directly 
def get_nic_stats(itf_idx, is_rx, namespace=None):
    global realtime_telem, stop_collection, snapshot_lock
    direction = "RX" if is_rx else "TX"
    with IPRoute(netns=namespace) if namespace else IPRoute() as ipr:
        while not stop_collection.is_set():
            # Request specific link info including binary attributes
            link_data = ipr.get_links(itf_idx)[0]
            # Convert attributes tuple list into a workable dictionary
            attrs = dict(link_data['attrs'])        
            # 'IFLA_STATS64' contains the exact mirror of the /sys/../statistics files and returns the 64 bit version
            stats = attrs.get('IFLA_STATS64')
            with snapshot_lock:
                for stat in stats.keys():
                    if stat in STAT_NAMES[direction]:
                        realtime_telem[direction][stat] = (time.time(), stats.get(stat, 0)) 
            time.sleep(0.1)

def get_nic_stats_snapshot(is_rx):
    global realtime_telem, snapshot_lock
    with snapshot_lock:
        return realtime_telem["RX" if is_rx else "TX"].copy()
       
def generate_traffic(rx_ip, tx_ip, tx_intf, bitrate, duration):
    # 2. Run iperf3 to saturate the link (using UDP to force queue pressure)
    print(f"Saturating link to {rx_ip} via {tx_intf}...")
    cmd = ["iperf3", "-c", rx_ip, "-B", tx_ip, "--bind-dev", tx_intf, "-u", "-b", bitrate, "-t", str(duration), "-J"]
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

def scan_interfaces():
    # Get all network interface addresses
    interfaces = psutil.net_if_addrs()
    # Define common prefixes for wired and wireless interfaces
    wired_prefixes = ('eth', 'en', 'em', 'br', 'lan')
    wireless_prefixes = ('wlan', 'wlp', 'wifi', 'wlan')
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
    
def get_intf_info(intf_name):
    # Check if the interface is active/running
    stats = psutil.net_if_stats()
    interfaces = psutil.net_if_addrs()
    addresses = interfaces[intf_name]
    if intf_name in stats:
        is_up = stats[intf_name].isup
        print(f"  Status: {'UP' if is_up else 'DOWN'}")
        print(f"  Speed: {stats[intf_name].speed}Mb/s")
        driver_dir = Path("/sys/class/net") / intf_name / "device" / "driver"
        actual_path = os.path.realpath(driver_dir)
        # os.path.basename grabs just the trailing folder name
        driver_name = os.path.basename(actual_path)
        print(f"Driver: {driver_name}")
        for address in addresses:
            # psutil.AF_LINK represents the MAC address family
            if address.family == psutil.AF_LINK:
                print(f"MAC: {address.address}")

def calculate_deltas(initial, final):
    delta = {}
    for key in initial:
        delta[key] = final[key][1] - initial[key][1]
    return delta

def run_test(rx_intf_idx, tx_intf_idx):
    global stop_collection, tx_interface, threads
    # 1. Snapshot L2 stats before the stress test
    ms_start = int(time.time() * 1000)
    initial_l2_rx = get_nic_stats_snapshot(True)
    initial_l2_tx = get_nic_stats_snapshot(False)
    # 2. generate traffic
    summary = generate_traffic(rx_ip, tx_ip, tx_interface, bitrate, duration)
    stop_collection.set()
    for thread in threads:
        thread.join()
    # 3. Snapshot L2 stats immediately after the test
    final_l2_rx = get_nic_stats_snapshot(True)
    final_l2_tx = get_nic_stats_snapshot(False)
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


def launch_server():
    # sudo ip netns exec rxbench iperf3 -s -B 172.31.250.2
    cmd = ["ip", "netns", "exec", rx_namespace, "iperf3", "-s", "-1", "-B", rx_ip]
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    
if __name__ == "__main__":
    scan_interfaces()
    get_intf_info(rx_interface)
    get_intf_info(tx_interface)
    rx_itf_idx = get_interface_index(rx_interface, rx_namespace)
    tx_itf_idx = get_interface_index(tx_interface)
    create_rx_namespace(rx_itf_idx)
    threads = [
        threading.Thread(target=get_nic_stats, args=(rx_itf_idx, True, rx_namespace)),
        threading.Thread(target=get_nic_stats, args=(tx_itf_idx, False))
    ]
    for thread in threads:
        thread.start()
    launch_server()
    run_test(rx_itf_idx, tx_itf_idx)
