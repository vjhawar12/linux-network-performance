# Diagnostic — Linux Kernel pktgen

**Status: planned; supporting tool, not a numbered packet-engine stage.** Return to the [main roadmap](../../README.md).

## Goal

Use kernel `pktgen` as an **independent packet-rate reference** when the userspace sender may be limiting achieved frames/s. Compare on the same approved NIC–switch–NIC physical topology.

## Proposed experiments

- Verify `pktgen` availability and configuration under `/proc/net/pktgen`.
- Configure physical interface/queue, thread/CPU, packet size, offered count/rate and duration.
- Measure TX and RX frame counts, driver/NIC errors, CPU use, achieved frames/s and available external switch counters.
- Compare under equivalent frame lengths and test conditions with iperf3, AF_PACKET and later engines.

`pktgen` traffic generation does **not** prove frames were received or forwarded by the switch. It can be disruptive and may require privileges; use only isolated approved links.

## Deliverable

A reproducible independent baseline and an explanation of which host, kernel, NIC and switch limitations it can and cannot identify.
