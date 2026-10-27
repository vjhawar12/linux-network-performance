import psutil
from pathlib import Path
import subprocess
import os
import argparse
import json
import time
from pyroute2 import NetNS

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

def read_from_vf(stats_dir, is_rx):
    # Read a single integer directly from its own virtual file
    if is_rx:
        return {
            name: int((stats_dir / name).read_text().strip())
            for name in STAT_NAMES["RX"]
        }
    else:
        return {
            name: int((stats_dir / name).read_text().strip())
            for name in STAT_NAMES["TX"]
        }

# reading virtual files in /sys/class/net/<interface>/statistics to inspect rx/tx drops/errors
# iperf3 fundamentally does the same thing but operates at layer 3 rather than our goal of layer 2 
def get_nic_stats(interface, is_rx, namespace):
    if namespace == DEFAULT_NAMESPACE:
        """Reads Layer 2 rx/tx drops and errors directly from the Linux kernel."""
        stats_dir = Path("/sys/class/net") / interface / "statistics"
        return read_from_vf(stats_dir, is_rx)
    else:
        """PyRoute switches the network namespace temporarily"""
        with NetNS("myns"):
            with open(Path("/sys/class/net") / interface / "statistics") as stats_dir:
                return read_from_vf(stats_dir, is_rx)

def generate_traffic(rx_ip, tx_ip, tx_intf, bitrate, duration):
    # 2. Run iperf3 to saturate the link (using UDP to force queue pressure)
    print(f"Saturating link to {server_ip} via {tx_intf}...")
    cmd = ["iperf3", "-c", rx_ip, "-B", tx_ip, "--bind-dev", tx_intf, "-u", "-b", bitrate, "-t", duration, "-J"]
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
        delta[key] = final[key] - initial[key]
    return delta

def run_test(rx_intf, tx_intf):
    # 1. Snapshot L2 stats before the stress test
    ms_start = int(time.time() * 1000)
    initial_l2_rx = get_nic_stats(rx_intf, True)
    initial_l2_tx = get_nic_stats(tx_intf, False)
    # 2. generate traffic
    summary = generate_traffic(rx_ip, tx_ip, tx_intf, bitrate, duration)
    # 3. Snapshot L2 stats immediately after the test
    final_l2_rx = get_nic_stats(rx_intf, True)
    final_l2_tx = get_nic_stats(tx_intf, False)
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
    print(f"Bits per second: {summary[bits_ps]}")
    print(f"Loss percent: {summary["lost_percent"]}")
    print(f"Jitter ms: {summary[jitter_ms]}")
if __name__ == "__main__":
    scan_interfaces()
    get_intf_info(rx_interface)
    get_intf_info(tx_interface)
    run_test(rx_interface, tx_interface)
