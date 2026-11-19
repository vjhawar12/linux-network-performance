from pyroute2 import IPRoute
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
        diff[key] = after[key] - before[key]
    return diff

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rx_intf_idx")
    parser.add_argument("--tx_intf_idx")
    args = parser.parse_args()
    rx_intf_idx = args.rx_intf_idx
    tx_intf_idx = args.tx_intf_idx
    # before, after
    telemetry_tx = (dict(), dict())
    telemetry_rx = (dict(), dict())
    benchmark_cmd = []
    # snapshot before
    with IPRoute() as ipr:    
        collect(tx_intf_idx, ipr, telemetry_tx[0])
        collect(rx_intf_idx, ipr, telemetry_rx[0])
    print(telemetry_rx[0])
    print(telemetry_tx[0])
    # use subprocess to run the app.cpp file 
    subprocess.run(benchmark_cmd)
    # snapshot after
    with IPRoute() as ipr:    
        collect(tx_intf_idx, ipr, telemetry_tx[1])
        collect(rx_intf_idx, ipr, telemetry_rx[1])
    print(telemetry_rx[1])
    print(telemetry_tx[1])
    # get differences
    diff_rx = get_diff(telemetry_rx[1], telemetry_rx[0])
    diff_tx = get_diff(telemetry_tx[1], telemetry_tx[0])
    print(diff_rx)
    print(diff_tx)
