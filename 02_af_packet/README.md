# Stage 2 — AF_PACKET Raw Ethernet Experiment

Stage 2 investigates the shortest packet path used by this project so far. The C++ sender and receiver exchange Ethernet frames with `AF_PACKET`, removing IP routing, UDP processing, and UDP socket queues from the workload. The Linux packet-socket queues, driver, and interface path still matter for loss attribution. [Stage 1](../01_baseline/README.md) provides the UDP/IP benchmark for comparison.

## What the experiment measures

- The sender builds 1,490-byte Ethernet frames with experimental EtherType `0x88B5`. The 1,476-byte Ethernet payload reserves space for an 8-byte run ID and an 8-byte sequence number.
- The sender calls `sendto()` until it reaches the frame budget calculated from the configured frame bitrate and duration, or its deadline expires. The current loop submits frames as quickly as it can within those bounds.
- The receiver runs in a separate thread, binds to the RX interface and experimental EtherType, and counts successful reads. The Python harness captures TX and RX interface counters before and after the run.
- The executable reports requested frames, frames submitted, frames read, elapsed time, and RX Ethernet payload throughput. The reported loss percentage compares the configured receive budget with successful reads. The receiver currently counts frames by EtherType, and per-run ID and sequence validation are future work.

Check submitted and read counts separately. The gap between them can help locate the next measurement, and interface counters can show whether traffic reached the receiving interface. When the generator falls short of the frame budget, that shortfall also affects the displayed loss percentage.

## Run a local veth test

From the repository root on Linux, install CMake 3.23 or later, a C++17 compiler, `iproute2`, and Python with venv support. The launcher builds the repository, brings up `veth-tx` and `veth-rx`, and runs the Python counter harness. Creating the pair and opening raw sockets use `sudo`.

```bash
python3.12 -m venv 02_af_packet/.venv312
02_af_packet/.venv312/bin/python3 -m pip install pyroute2
bash 02_af_packet/run.sh
```

[`_launch.sh`](_launch.sh) runs the C++ executable with TX interface, RX interface, source MAC, destination MAC, message, frame bitrate in bit/s, duration in seconds, and run ID. The receiver currently uses a fixed 10-second deadline. The local test uses a virtual Ethernet pair, and physical switch forwarding remains a separate validation step.

To inspect frames while the test runs, capture the experimental EtherType on `veth-rx`:

```bash
sudo tcpdump -i veth-rx -n -e 'ether proto 0x88b5'
```

An earlier sender revision was validated with `tcpdump` across a WSL2 veth pair. The current build uses [`app.cpp`](app.cpp), [`packet_generator.cpp`](packet_generator.cpp), and [`packet_receiver.cpp`](packet_receiver.cpp). The Python wrapper is [`test_harness.py`](test_harness.py).

## Next experiments

1. Validate run IDs and sequence numbers at the receiver. Report missing, duplicate, and out-of-order frames against the number actually submitted.
2. Read `PACKET_STATISTICS` on the RX socket to test whether a gap between interface packets and receiver reads is associated with packet-socket queue drops.
3. Compare UDP/IP and raw Ethernet at matched frame lengths, offered packet rates, and durations. Record actual packet rates, received throughput, loss, and CPU use.
4. Measure the effect of batching, memory-mapped packet rings, and TX/RX CPU affinity before moving to the [AF_XDP](../03_af_xdp/README.md) and [DPDK](../04_dpdk/README.md) experiments.

[Project overview](../README.md) · [Shared architecture](../common/README.md)
