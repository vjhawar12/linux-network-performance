# Phase B — Linux Kernel pktgen

**Status: planned. No implementation in this directory yet.**

## Goal

Use the Linux kernel's `pktgen` facility to explore high packet-per-second workloads that may be limited by a userspace iperf3 sender. Compare results with the [Phase A baseline](../01_baseline/README.md) on the **same confirmed physical switch path**.

## Proposed work

- Review `pktgen` kernel module availability and control through `/proc/net/pktgen`.
- Choose the transmitting NIC/queue, frame sizes, thread/CPU mapping, and offered traffic volume.
- Measure achieved NIC TX rates, receiver-side counts/drops, CPU load, and (if accessible) LAN9693 port counters.
- Evaluate sensitivity to small frames, queue mapping, and CPU placement.

`pktgen` sending packets is **not by itself proof of switch forwarding or end-to-end reception**. The receiver and switch must still be observed. Configuring pktgen may require root access and can generate very high traffic loads.

## Entry criteria

Complete Phase A's route/cabling verification and have trustworthy interface counter deltas and saved benchmark reports. Keep setup and teardown explicit to avoid accidentally transmitting over the management network.

## Exit criteria

Document reproducible pktgen workloads, the observed sender/receiver rates and drops, and a comparison to the Phase A baseline.
