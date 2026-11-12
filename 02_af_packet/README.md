# Stage 2 — C++ AF_PACKET Raw Ethernet

**Status: planned — no generator implemented yet.** Replace Stage 1's iperf3 **traffic engine** with a custom C++ Layer 2 sender and matching receiver. Python remains the experiment runner, progressively losing responsibilities that belong in the native hot path.

[Main roadmap](../README.md) · [Stage 1](../01_baseline/README.md) · [Shared design](../common/README.md)

## Engineering question

Can we better control Ethernet frame size, rate, payload content, sequence accounting and switch forwarding by generating raw L2 traffic? Does any measured reduction in conventional UDP/IP processing improve throughput or CPU efficiency?

AF_PACKET still sends through the **Linux packet-socket and driver path**. It is neither true kernel bypass nor automatically faster/more accurate than iperf3.

## Data path

```text
Python runner -> C++ AF_PACKET TX -> Linux driver -> TX NIC
                                     external switch
Python results <- C++ AF_PACKET RX <- Linux driver <- RX NIC
```

Use `AF_PACKET` / `SOCK_RAW` with the selected physical interface (normally requires `CAP_NET_RAW`). Construct Ethernet source/destination MAC fields and a designated EtherType; no IPv4 or UDP header is required. For this custom L2 workload, no IP address or separate network namespace is inherently needed, although the test must prove the actual switch path and isolate other traffic.

## Proposed frame and features

- Reusable frame buffers: Ethernet header, versioned test EtherType/payload, run ID, sequence number, optional timestamp and configurable test pattern.
- Configurable Ethernet frame length, target packet rate or bitrate, duration, source/destination MACs, and direction.
- Matching receiver with missing/duplicate/out-of-order packet accounting; an iperf3 server **will not** interpret the custom protocol.
- Precise distinction among payload size, frame length (including FCS where relevant), and on-wire overhead.
- Correct shutdown, backend status and machine-readable results that Python can ingest.

Begin with straightforward socket TX/RX. Once correct, profile and experiment with `sendmmsg()`, `PACKET_TX_RING`/`PACKET_RX_RING`, memory preallocation, and batching as justified.

## Comparison and exit criteria

Reuse the same physical test path and comparable actual Ethernet frame sizes as Stage 1. Compare achieved frame rate, bandwidth, CPU utilization, receive sequence gaps, and independent NIC/switch counters. **Receiver loss does not pinpoint the switch as the cause.**

Initial validation target: 10 GbE. AF_PACKET does **not** have a hard 10 Gb/s ceiling. The canonical custom test frame format should be shared with [Stages 3](../03_af_xdp/README.md) and [4](../04_dpdk/README.md).
