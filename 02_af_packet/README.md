# Stage 2 — AF_PACKET Raw Ethernet Experiment

Stage 2 explores raw Ethernet traffic generation and reception using Linux `AF_PACKET` sockets. Unlike [Stage 1](../01_baseline/README.md), which benchmarks UDP/IP using `iperf3`, this experiment bypasses IP routing and UDP processing. It does **not** bypass the Linux kernel, packet-socket receive queues, or the network device path. The goal is to study raw frame throughput and distinguish application delivery shortfalls from socket- and interface-level drops.

## Current implementation

- **Frames:** The sender builds 1,490-byte Ethernet frames using experimental EtherType `0x88B5`. The 1,476-byte payload contains an 8-byte run ID, an 8-byte sequence number, and a message. The sender increments the sequence number on successful `sendto()` calls.
- **Transmission:** A C++17 generator calls `sendto()` in a tight loop until it reaches a requested frame budget or a shared deadline. The configured bitrate determines the **frame budget**; it does **not** pace transmissions at that rate.
- **Reception:** A second thread binds an `AF_PACKET` socket to the RX interface and EtherType. A blocking `recv()` uses `SO_RCVTIMEO` (20 ms) to periodically reevaluate the generator-completion flag. The sender and receiver share a deadline, followed by a 100 ms receive-drain phase.
- **Errors:** The generator and receiver report unexpected socket errors through exceptions. Receiver-thread exceptions are captured and rethrown in the main thread after joining.
- **Statistics:** The application prints requested frames, successful `sendto()` submissions, successful `recv()` reads, RX payload throughput, and the RX socket's `PACKET_STATISTICS` (`tp_packets`, `tp_drops`). The Python harness captures before/after Netlink `IFLA_STATS64` counters for both veth interfaces.

### Interpreting the results

| Metric | Interpretation |
| --- | --- |
| Frames Requested | Frame budget derived from bitrate, duration, and 1,490-byte frame size |
| Frames Submitted (sendto) | Frames successfully submitted to the kernel; **not** proof of physical transmission |
| Frames Read (recv) | Successful socket reads; currently **not** unique, run-validated frames |
| TP Packets / TP Drops | Counters from the RX packet socket, with `tp_drops` identifying packet-socket receive drops |
| Netlink TX/RX packets and drops | Interface-level counters, not independent additive loss categories |
| RX Payload Throughput | Bytes read multiplied by 8, divided by measured generator elapsed time; the receiver additionally drains for up to 100 ms |

The reported **Loss percentage** uses the difference between successful sender submissions and receiver reads, divided by successful submissions. Until the receiver validates run IDs and sequence numbers, treat this as a **read-count shortfall**, not proven end-to-end unique-frame loss. Do not add `tp_drops`, interface drops, and the read-count shortfall together; they can overlap in the packet path.

**Known limitation:** In the current revision, the loss calculation displays `0%` if zero frames are received, even when frames were submitted. Fix this guard before interpreting a zero-receive benchmark. Runs with zero submissions should report loss as unavailable, not 0%.

## Build and smoke test (Linux)

Requirements: CMake 3.23+, a C++17 compiler, `iproute2`, and privileges to create veth interfaces and open raw packet sockets.

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j

# Create the pair once if it does not already exist:
sudo ip link add veth-tx type veth peer name veth-rx
sudo ip link set veth-tx up
sudo ip link set veth-rx up

# Approximately 1,000 frames of 1,490 bytes, with a 1-second budget:
sudo ./build/linux_network_performance \
    veth-tx veth-rx \
    "$(cat /sys/class/net/veth-tx/address)" \
    "$(cat /sys/class/net/veth-rx/address)" \
    Hello 11920000 1 1
```

Verify that the process exits without hanging; submitted/read counts are plausible; and `TP Packets` / `TP Drops` are reported. The requested frame count is a **maximum budget**: the actual number sent depends on the deadline and execution speed.

For the Python wrapper with interface-statistics snapshots:

```bash
python3.12 -m venv 02_af_packet/.venv312
02_af_packet/.venv312/bin/python3 -m pip install pyroute2
bash 02_af_packet/run.sh
```

**Note:** `run.sh` invokes `_launch.sh`, which currently requests **5 Gb/s for 10 seconds**; use the direct command above for the first low-rate test. The harness executes the C++ binary, then reports the difference between the pre-test and post-test interface counters. It is **not yet** a continuous telemetry collector.

Optional frame inspection:

```bash
sudo tcpdump -i veth-rx -n -e 'ether proto 0x88b5'
```

The local veth pair is a virtual Linux link, not a physical 10GbE switch or NIC benchmark.

## Next experiments

1. Validate the receiver's run ID and sequence numbers; calculate unique-frame loss, duplicates, and out-of-order delivery against **actual successful submissions**.
2. Fix the zero-receive loss percentage, exercise receiver error propagation, and verify the socket and interface counters under low and high loads.
3. Reuse the [Stage 1 Streamlit dashboard](../01_baseline/display_gui.py) for Stage 2 **after** adding periodic Stage 2 telemetry to the harness. Plot TX/RX interface throughput and drops; display `PACKET_STATISTICS` separately, because it is currently sampled only at completion and reading it resets those socket counters. Do not reuse Stage 1's UDP `RcvBufErrors` series for raw packet sockets.
4. Compare Stage 1 and Stage 2 at matched frame lengths, offered loads, and durations, recording CPU utilization; then investigate receive batching, `PACKET_RX_RING`, and CPU affinity before considering [AF_XDP](../03_af_xdp/README.md) or [DPDK](../04_dpdk/README.md).

[Project overview](../README.md) · [Shared architecture](../common/README.md)
