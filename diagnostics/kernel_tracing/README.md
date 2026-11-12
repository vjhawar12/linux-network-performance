# Diagnostic — Kernel and NIC Performance Tracing

**Status: planned; supporting analysis, not a numbered traffic-engine stage.** Return to the [main roadmap](../../README.md).

## Goal

Explain packet-rate limits and losses using Linux and device observability, instead of attributing everything to the external switch or PHY.

## Investigation areas

- NIC queue and driver counters (`ethtool -S`; rtnetlink statistics when available).
- Interrupt and CPU distribution (`/proc/interrupts`, `/proc/softirqs`, softnet/NAPI activity).
- CPU scheduling, cache/memory effects, socket buffers, dropped packets, driver queue pressure and offloads.
- `perf`, kernel tracing and eBPF as supported, with measurement overhead recorded.
- DPDK- or AF_XDP-specific diagnostics when their device paths are not exposed through ordinary Linux interface counters.
- Cross-correlation with switch ingress/egress counters, link events, and shaker-table telemetry.

Tracing may perturb the workload; record tracing configuration, platform and tools. Application-level receive loss alone does not isolate whether the sender, host, physical link, switch or receiver dropped traffic.

## Deliverable

A documented performance hypothesis supported by multiple independent measurements and a reproducible before/after comparison.
