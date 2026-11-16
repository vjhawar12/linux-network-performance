# Stage 2 — C++ AF_PACKET Raw Ethernet

**Status (14 November 2026): transmitter implemented; repeated-transmission validation in progress.** The source is [`packet_generation.cpp`](packet_generation.cpp). An earlier version of the raw-socket sender successfully transmitted an experimental Ethernet frame across a WSL2 `veth` pair, verified with `tcpdump`. The newer `PacketGenerator` send loop and the Netlink discovery function are present in source but have **not yet been independently validated end-to-end**. There is no native receive engine or measured throughput/loss report yet.

[Main roadmap](../README.md) · [Stage 1](../01_baseline/README.md) · [Shared design](../common/README.md)

## Engineering question

Can a controlled Layer-2 workload help characterize NIC, host, physical-link and external-switch forwarding behavior? How do packet rates, CPU utilization and loss accounting compare with the Stage 1 UDP/IP baseline?

`AF_PACKET` uses the Linux packet-socket and driver path; it is **not kernel bypass** and is not inherently faster or more accurate than `iperf3`. Performance and loss attribution require independent measurements.

## Implemented now

- **`MacAddress`:** parses CLI-supplied Ethernet MAC addresses into six-byte values.
- **`EthernetFrame<capacity>`:** builds an Ethernet header and text payload in a fixed-size buffer. The current experimental EtherType is `0x88B5` (`ETH_P_802_EX1`); the active `main()` instantiates a 256-byte maximum payload capacity.
- **`PacketSocket`:** opens an `AF_PACKET` / `SOCK_RAW` socket, prepares `sockaddr_ll` for a selected TX interface and destination MAC, closes its file descriptor on destruction, deletes copying, and supports explicit moves.
- **`PacketGenerator<capacity>`:** holds one socket and one prebuilt frame and calls `sendto()` repeatedly for a runtime-configured `uint64_t` packet count. Frames are currently identical; no pacing, sequence numbers or per-run metrics are implemented.
- **Experimental interface discovery:** `discover()` requests an `RTM_GETLINK` dump over `NETLINK_ROUTE`, parses `RTM_NEWLINK` attributes and receives datagrams until `NLMSG_DONE`. It is not called from `main()`; parsing bounds checks, truncation handling and independent functional validation remain outstanding. The active send path selects TX by `if_nametoindex()`.

The code is a work-in-progress C++17 prototype, not a production-ready benchmark tool.

## Current data path

```text
C++ CLI
  -> PacketGenerator (reuses one EthernetFrame)
  -> AF_PACKET SOCK_RAW / sendto()
  -> Linux packet socket + driver
  -> TX virtual or physical NIC
  -> peer interface / external switch (depending on test topology)
  -> tcpdump for current RX-side observation
```

The intended future path will replace `tcpdump` with a C++ receiver and report both sender and receiver counters. The Python orchestration integration has not been implemented for this backend.

## Build and local smoke test

Linux with a C++17 compiler and sufficient permissions (`CAP_NET_RAW` for sending, and administrative privileges to create `veth` devices) is required.

From the repository root:

```bash
g++ -std=c++17 -O2 -Wall -Wextra \
    02_af_packet/packet_generation.cpp -o packet_sender
```

Create an isolated local pair for a functional test (skip creation if these interfaces already exist):

```bash
sudo ip link add veth-tx type veth peer name veth-rx
sudo ip link set veth-tx up
sudo ip link set veth-rx up
ip link show veth-tx
ip link show veth-rx
```

In terminal 1, capture ten matching frames on the receiver-side virtual interface:

```bash
sudo tcpdump -i veth-rx -n -e -XX -c 10 'ether proto 0x88b5'
```

In terminal 2, transmit ten text-payload frames. The CLI currently expects **TX interface, source MAC, destination MAC, text payload, packet count**, in that order:

```bash
sudo ./packet_sender veth-tx \
    "$(cat /sys/class/net/veth-tx/address)" \
    "$(cat /sys/class/net/veth-rx/address)" \
    "Hello" 10
```

This is a **test procedure, not a claim that the latest loop passed it**. Compare packet count, source/destination MACs, EtherType and payload in the capture. A successful `sendto()` means a frame was submitted to the kernel, not that a downstream switch or receiver actually delivered it. Repeat with a modest larger count only after the ten-frame test passes.

The current program has no named CLI flags, `--list-interfaces` mode or dedicated build system for Stage 2. A text payload longer than 256 bytes is rejected by `EthernetFrame`; binary payloads containing NUL bytes are not yet supported because the generator uses `strlen()`.

## Validation status

| Capability | Status |
| --- | --- |
| Construct custom Ethernet frame with EtherType `0x88B5` | Implemented |
| Transmit one raw frame across WSL2 `veth` and inspect with `tcpdump` | **Validated in an earlier sender revision** |
| Reuse socket and transmit a configurable count of frames | Implemented; **not yet verified after integration** |
| Discover interfaces using rtnetlink | Implemented as prototype; **not yet verified or connected to CLI** |
| Native RX engine, sequence numbers and missing/duplicate accounting | Not implemented |
| TX timing, packets/s, Mbps and structured results | Not implemented |
| Physical NIC → external switch → physical NIC benchmark | Not validated |
| 10 GbE or higher throughput claims | None |

## Next steps

1. **Functional baseline:** compile and run the ten-frame `veth` test, then a larger repeated-send test. Confirm output with `tcpdump`, not just successful syscall returns.
2. **Reliability pass:** validate the interface dump against `ip link`, check Netlink message/attribute bounds, improve error and resource handling, and check command-line arguments and partial sends.
3. **Measurement path:** add timestamps, send counts, payload sizes, sequence numbers and a C++ receiver with RX/loss/ordering reports.
4. **Benchmark integration:** have Python orchestrate experiments and capture results; validate the actual physical switch forwarding path and correlate host, NIC and switch statistics.
5. **Optimization after baselines:** profile `sendto()` and evaluate batching, `sendmmsg()`, `PACKET_TX_RING`/`PACKET_RX_RING` or later AF_XDP/DPDK as measurements justify them.

## Measurement and safety notes

- Application TX submission rate is not wire throughput or verified RX throughput.
- Missing frames alone cannot identify whether loss occurred in the sender, kernel, NIC, physical link, switch or receiver.
- On-wire Ethernet overhead matters for line-rate comparisons. Virtual `veth` tests do not validate real Ethernet padding, FCS or physical NIC/switch performance.
- Use only approved, isolated test interfaces. Do not generate high-rate test traffic on management or shared networks.
