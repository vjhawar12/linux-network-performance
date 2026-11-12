# Stage 4 — C++ DPDK High-Performance Packet Engine

**Status: long-term plan.** Keep Python lean: configure experiments, supervise a C++ DPDK engine, aggregate telemetry and produce results. C++ owns the high-rate frame TX/RX path and its memory/queue resources.

[Main roadmap](../README.md) · [Stage 3](../03_af_xdp/README.md) · [Shared design](../common/README.md)

## Engineering question

What scaling limits arise from NIC queues, PCIe bandwidth, CPU cores, cache/memory bandwidth, NUMA locality and driver design, and how far can a tuned userspace packet engine push a verified host–switch–host path?

The 100–400 GbE target is a **long-term hardware ambition**, not a guaranteed outcome or a hard limit of DPDK (which also supports lower-speed NICs).

## Intended implementation

```text
Python experiment runner
          |
C++ DPDK TX/RX engine
   mbuf pools / burst APIs / lcore & queue assignments
          |
DPDK PMD and compatible NIC -> external switch -> NIC
          |
DPDK counters + receiver stats -> Python reporting
```

1. Verify compatible NIC/PMD and kernel/device-access arrangement; document hugepages, IOMMU, PCIe topology, NIC binding and management-plane safety.
2. Reuse Stages 2–3's **identical L2 test-frame format**, workload definitions and receive sequence accounting.
3. Implement correct single-queue `rte_mbuf` ownership, `rte_eth_tx_burst()` and `rte_eth_rx_burst()`, pacing and clean stop.
4. Add measured multi-queue/multicore scaling, CPU affinity, NUMA-aware allocation, batch sizes, offloads and backpressure.
5. Compare against Stage 3 and earlier engines with the same physical path, frame lengths and target loads.
6. Expand to 100G, 200G or 400G only with confirmed hardware, cabling and measured results.

## Telemetry and caveats

A NIC controlled by DPDK **may not expose the normal Linux network interface** (e.g. some `vfio-pci` configurations). Not every PMD behaves identically. The Python telemetry layer must support backend-specific DPDK port and extended stats alongside independent NIC/switch observations; it cannot require rtnetlink on every port.

Avoid binding a management interface to a DPDK driver or sending disruptive traffic over shared links. DPDK does not by itself prove that loss originates in the switch or PHY.

## Exit criteria

A reproducible C++ engine with sequence-based receiver results, native port/queue counters and documented frame-rate/CPU scaling. The result should explain **which bottlenecks were removed and which remain**.
