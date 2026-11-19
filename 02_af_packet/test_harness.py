from pyroute2 import IPRoute, netns, IPRSocket
from pyroute2.netlink.rtnl import RTMGRP_LINK
import time
import argparse
import threading

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

def app(intf_idx, telemetry):
    with IPRoute() as ipr:    
        collect(intf_idx, ipr, telemetry)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rx_intf_idx")
    parser.add_argument("--tx_intf_idx")
    args = parser.parse_args()
    rx_intf_idx = args.rx_intf_idx
    tx_intf_idx = args.tx_intf_idx
    telemetry_tx = dict()
    telemetry_rx = dict()
    tx_thread = threading.Thread(target=app, args=(tx_intf_idx, telemetry_tx,))
    tx_thread.start()
    app(rx_intf_idx, telemetry_rx)
    tx_thread.join()
    print(telemetry_rx)
    print(telemetry_tx)
