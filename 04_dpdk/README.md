# Stage 4 — DPDK Userspace Packet I/O Experiment

**Status: planned.** This experiment continues the raw Ethernet investigation from [AF_PACKET](../02_af_packet/README.md) and [AF_XDP](../03_af_xdp/README.md). It studies how a userspace polling driver changes queue processing, loss visibility, and CPU cost for the same workload. Python will configure runs and collect results, while C++ will own frame TX/RX and queue resources.

[Main roadmap](../README.md) · [Stage 3](../03_af_xdp/README.md) · [Shared design](../common/README.md)

## Engineering question

What scaling limits arise from NIC queues, PCIe bandwidth, CPU cores, cache/memory bandwidth, NUMA locality and driver design, and how far can a tuned userspace packet engine push a verified host–switch–host path?

Begin on available hardware with matched AF_XDP and DPDK workloads. Testing at 100–400 GbE depends on suitable hardware and a verified physical forwarding path.

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

For each PMD, document which statistics are available. Some configurations expose port and queue counters through DPDK directly, so the Python collector should support DPDK statistics and independent switch-port observations alongside any available Linux interface counters.

Use dedicated test interfaces and correlate native DPDK counters with the receiving application and switch ports to attribute losses.

## Exit criteria

A reproducible C++ engine with sequence-based receiver results, native port/queue counters and documented frame-rate/CPU scaling. The result should explain **which bottlenecks were removed and which remain**.
