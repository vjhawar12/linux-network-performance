# Phase E — Advanced Packet I/O

**Status: optional / planned. No implementation in this directory yet.**

## Goal

Assess whether higher-performance packet paths such as **AF_XDP** or **DPDK** are warranted after establishing a trustworthy baseline and investigating kernel networking bottlenecks.

## Possible experiments

- Compare standard Linux sockets to AF_XDP or DPDK for selected packet sizes and rates.
- Record throughput, packets per second, CPU utilization, drops, memory use, and operational complexity.
- Examine queue assignment, busy-polling, batching, DMA/memory requirements, and offloads.
- Verify hardware, NIC driver, kernel, and deployment prerequisites before committing to either approach.

These technologies are **not assumed to be supported or faster** on the exact test configuration. In particular, DPDK can change how a NIC is bound/managed and should not be experimented with on the host management interface.

## Entry criteria

Confirmed physical path and saved [Phase A](../01_baseline/README.md) results; evidence from earlier phases that conventional Linux I/O is the limiting factor.

## Exit criteria

A documented apples-to-apples comparison and recommendation about whether advanced packet I/O is worthwhile for the LAN9693 validation workload.
