import subprocess

# sudo ip netns exec rxbench iperf3 -s -B 172.31.250.2
def launch_server(rx_namespace, rx_ip):
    subprocess.Popen([
        "ip", "netns", "exec", rx_namespace, 
        "iperf3", "-s", "-1", "-B", rx_ip
        ], 
        stdout=subprocess.DEVNULL, 
        stderr=subprocess.PIPE, 
        text=True
    )

# listens on the other end of the socket and sends packets at the specified rate
def generate(rx_ip, tx_ip, tx_intf_name, bitrate, duration):
    return ["iperf3", "-c", rx_ip, "-B", tx_ip, "--bind-dev", tx_intf_name, "-u", "-b", bitrate, "-t", str(duration), "-J", "-P", "1"]
    