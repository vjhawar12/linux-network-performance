import psutil
import iperf3

# Get all network interface addresses
interfaces = psutil.net_if_addrs()

# Define common prefixes for wired and wireless interfaces
wired_prefixes = ('eth', 'en', 'em', 'br', 'lan')
wireless_prefixes = ('wlan', 'wlp', 'wifi', 'wlan')

# Filter for interfaces that match wired prefixes and ignore wireless/loopback
wired_interfaces = {
    name: addrs for name, interfaces.items()
    if name.lower().startswith(wired_prefixes) 
    and not name.lower().startswith(wireless_prefixes)
    and name != 'lo'
}


def get_l2_drop_stats(interface):
    """Reads Layer 2 rx/tx drops and errors directly from the Linux kernel."""
    with open("/proc/net/dev", "r") as f:
        lines = f.readlines()
    for line in lines:
        if interface in line:
            # /proc/net/dev format:
            # face |bytes packets errs drop fifo frame ...
            data = line.split()
            rx_errs = int(data[2])
            rx_drop = int(data[3])
            tx_errs = int(data[10])
            tx_drop = int(data[11])
            return {
                "rx_errors": rx_errs,
                "rx_drops": rx_drop,
                "tx_errors": tx_errs,
                "tx_drops": tx_drop,
            }
    return None

def run_test():
    # Get interface status (is up, speed, etc.)
    stats = psutil.net_if_stats()
    print("Available Network Interfaces:\n")
    for intf_name, addresses in wired_interfaces.items():
        print(f"Interface: {intf_name}")
        # Check if the interface is active/running
        if intf_name in stats:
            is_up = stats[intf_name].isup
            print(f"  Status: {'UP' if is_up else 'DOWN'}")
            print(f"  Speed: {stats[intf_name].speed}MB")
        if is_up:
            # 1. Snapshot L2 stats before the stress test
            initial_l2 = get_l2_drop_stats(itnf_name)
            # 2. Run iperf3 to saturate the link (using UDP to force queue pressure)
            print(f"Saturating link to {server_ip} via {itnf_name}...")
            cmd = ["iperf3", "-c", server_ip, "-u", "-b", "1G", "-t", "5", "-J"]
            result = subprocess.run(cmd, capture_output=True, text=True)
            # 3. Snapshot L2 stats immediately after the test
            final_l2 = get_l2_drop_stats(itnf_name)
            # 4. Calculate deltas
            l2_rx_dropped = final_l2["rx_drops"] - initial_l2["rx_drops"]
            l2_rx_errors = final_l2["rx_errors"] - initial_l2["rx_errors"]
            print("\n=== LAYER 2 NIC STATISTICS (Hardware/Driver) ===")
            print(f"NIC Interface Drops during test: {l2_rx_dropped}")
            print(f"NIC Interface Errors (CRC/Frame): {l2_rx_errors}")
        print("-" * 30)

if __name__ == "__main__":
    run_test()