import psutil
from pathlib import Path
import subprocess
import os
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("interface", type=str, help="name of the wired interface to performance-test")
parser.add_argument("server_ip", type=str, help="iperf3 server IP address")
args = parser.parse_args()
interface = args.interface
server_ip = "10.1.10.106"
if args.server_ip:
    server_ip = args.server_ip
# reading virtual files in /sys/class/net/<interface>/statistics to inspect rx/tx drops/errors
# iperf3 fundamentally does the same thing but operates at layer 3 rather than our goal of layer 2 
def get_l2_drop_stats(interface):
    """Reads Layer 2 rx/tx drops and errors directly from the Linux kernel."""
    stats_dir = Path("/sys/class/net") / interface / "statistics"

    # Read a single integer directly from its own virtual file
    rx_dropped = int((stats_dir / "rx_dropped").read_text().strip())
    rx_errors = int((stats_dir / "rx_errors").read_text().strip())
    tx_dropped = int((stats_dir / "tx_dropped").read_text().strip())
    tx_errors = int((stats_dir / "tx_errors").read_text().strip())

    print(f"L2 RX Drops on {interface}: {rx_dropped}")
    print(f"L2 RX Errors on {interface}: {rx_errors}")
    print(f"L2 TX Drops on {interface}: {tx_dropped}")
    print(f"L2 TX Errors on {interface}: {tx_errors}")

    return {
            "rx_errors": rx_errors,
            "rx_drops": rx_dropped,
            "tx_errors": tx_errors,
            "tx_drops": tx_dropped,
        }

def generate_traffic(server_ip, intf_name):
    # 2. Run iperf3 to saturate the link (using UDP to force queue pressure)
    print(f"Saturating link to {server_ip} via {intf_name}...")
    cmd = ["iperf3", "-c", server_ip, "--bind-dev", intf_name, "-u", "-b", "1G", "-t", "5", "-J"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    report = json.loads(result.stdout)
    if "error" in report:
        raise RuntimeError(report["error"])
    return report

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

def run_test(intf_name):
    # 1. Snapshot L2 stats before the stress test
    ms_start = int(time.time() * 1000)
    initial_l2 = get_l2_drop_stats(intf_name)
    generate_traffic(server_ip, intf_name)
    # 3. Snapshot L2 stats immediately after the test
    final_l2 = get_l2_drop_stats(intf_name)
    ms_end = int(time.time() * 1000)
    # 4. Calculate deltas
    l2_rx_dropped = final_l2["rx_drops"] - initial_l2["rx_drops"]
    l2_rx_errors = final_l2["rx_errors"] - initial_l2["rx_errors"]
    time_diff_ms = ms_end - ms_start 
    print("\n=== LAYER 2 NIC STATISTICS (Hardware/Driver) ===")
    print(f"NIC Interface Drops during test: {l2_rx_dropped}")
    print(f"NIC Interface Errors (CRC/Frame): {l2_rx_errors}")
    print(f"NIC Interface Test Duration (ms): {time_diff_ms}")


if __name__ == "__main__":
    scan_interfaces()
    get_intf_info(interface)
    run_test(interface)