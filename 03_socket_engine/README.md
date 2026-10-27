# Phase C — Custom Socket Traffic Engine

**Status: planned. No implementation in this directory yet.**

## Goal

Develop a small Linux C traffic generator/receiver to control packet formats, sending patterns, and loss accounting more directly than a generic bandwidth-testing utility.

## Proposed scope

- Define an explicit datagram format containing a sequence number and a test/run identifier.
- Use configurable source/destination addresses, ports, packet size, duration, and send rate.
- Investigate efficient socket APIs (for example `sendmmsg()` and `recvmmsg()`) and batching.
- Detect missing, duplicate, and reordered packets at the receiver.
- Measure userspace throughput, packet rate, CPU cost, and NIC counter deltas.

The C program should reuse the same **verified external-switch path** and namespace isolation principles as [Phase A](../01_baseline/README.md). Receiver sequence gaps need careful interpretation: they can reflect switch, NIC, kernel, or application-side loss, not automatically a hardware switch defect.

## Entry criteria

Validated Phase A baseline, documented packet-size/load settings, and working RX/TX network configuration.

## Exit criteria

Produce repeatable sender/receiver measurements and compare behavior against iperf3 under the same test conditions.
