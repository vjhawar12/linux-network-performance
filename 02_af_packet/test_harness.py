from pyroute2 import IPRoute
from pathlib import Path
import time
import argparse
import subprocess

class NetworkInterfaceException(Exception):
    pass

# call this regularly preferably in its own thread to track packet drop and errors
# inspect statistics like packet drop and throughput for a network interface 
# for interfaces within the default namespace reading the contents of the virtual file 
# /sys/class/net/<interface>/statistics is fine, but interfaces in alternate namespaces wouldn't show up there
# so this function uses pyroute2/netlink to fetch those same statistics, 
# but not by reading a virtual file directly 
def collect(intf_idx, ipr, telemetry):
    if ipr is None:
        raise NetworkInterfaceException("No instance of IPRoute passed!")
    # Request specific link info including binary attributes
    link_data = ipr.get_links(intf_idx)[0]
    # Convert attributes tuple list into a workable dictionary
    attrs = link_data['attrs']        
    # 'IFLA_STATS64' contains the exact mirror of the /sys/../statistics files and returns the 64 bit version
    stats = None
    for attr in attrs:
        if attr[0] == "IFLA_STATS64":
            stats = attr[1]
            break
    if stats is not None:
        for stat in stats.keys():
            if stat in telemetry:
                telemetry[stat] = (time.time(), stats.get(stat, 0))
    else:
        raise NetworkInterfaceException("Could not find IFLA_STATS64")

def get_diff(after, before):
    diff = dict()
    diff.update(after)
    for key in after:
        diff[key] = after[key][1] - before[key][1]
    return diff

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rx_intf_idx", type=int, required=True)
    parser.add_argument("--tx_intf_idx", type=int, required=True)
    args = parser.parse_args()
    rx_intf_idx = args.rx_intf_idx
    tx_intf_idx = args.tx_intf_idx
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
    # before, after
    telemetry_tx = (dict().fromkeys(STAT_NAMES["TX"]), dict().fromkeys(STAT_NAMES["TX"]))
    telemetry_rx = (dict().fromkeys(STAT_NAMES["RX"]), dict().fromkeys(STAT_NAMES["RX"]))
    # snapshot before
    with IPRoute() as ipr:    
        collect(tx_intf_idx, ipr, telemetry_tx[0])
        collect(rx_intf_idx, ipr, telemetry_rx[0])
    print(telemetry_rx[0])
    print(telemetry_tx[0])
    # use subprocess to run the app.cpp file 
    script_dir = Path(__file__).resolve().parent
    script_path = script_dir / "_launch.sh"
    result = subprocess.run([str(script_path)], cwd=script_dir, capture_output=True, text=True, check=True)
    # snapshot after
    with IPRoute() as ipr:    
        collect(tx_intf_idx, ipr, telemetry_tx[1])
        collect(rx_intf_idx, ipr, telemetry_rx[1])
    print(telemetry_rx[1])
    print(telemetry_tx[1])
    # get differences
    diff_rx = get_diff(telemetry_rx[1], telemetry_rx[0])
    diff_tx = get_diff(telemetry_tx[1], telemetry_tx[0])
    print("\n=== NIC STATISTICS ===")
    print(f"RX packets: {diff_rx['rx_packets']}")
    print(f"RX bytes: {diff_rx['rx_bytes']}")
    print(f"RX drops: {diff_rx['rx_dropped']}")
    print(f"RX errors: {diff_rx['rx_errors']}")
    print(f"RX CRC errors: {diff_rx['rx_crc_errors']}")
    print(f"RX missed: {diff_rx['rx_missed_errors']}")
    print(f"TX packets: {diff_tx['tx_packets']}")
    print(f"TX bytes: {diff_tx['tx_bytes']}")
    print(f"TX drops: {diff_tx['tx_dropped']}")
    print(f"TX errors: {diff_tx['tx_errors']}")
    print("\n=== PACKET GENERATOR SUMMARY ===")   
    print(result.stdout)