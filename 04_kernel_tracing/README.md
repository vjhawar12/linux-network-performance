# Phase D — Kernel Networking Tracing

**Status: planned. No implementation in this directory yet.**

## Goal

Explain *why* the host stops sustaining a target throughput or starts losing packets, using Linux kernel and driver observability rather than inferring the root cause from throughput alone.

## Proposed investigation areas

- NIC queue activity and interrupt distribution (`/proc/interrupts`).
- Softirq and NAPI processing (`/proc/softirqs`, `/proc/net/softnet_stat`).
- CPU scheduling and packet-processing load (for example `perf`, tracing facilities, or eBPF where supported).
- Driver statistics from `ethtool -S` and standard `/sys/class/net/.../statistics`.
- Ring/queue pressure, socket buffering, and the distinction between host drops and switch-side drops.

Not every kernel/driver exposes the same tracepoints or hardware counters. Enable instrumentation selectively: tracing overhead can affect the measurement itself.

## Entry criteria

A reproducible performance limitation or drop pattern from [Phase A](../01_baseline/README.md) (and optionally Phases B/C).

## Exit criteria

Document a measurable bottleneck hypothesis, supporting evidence from more than one layer, and any improvement after an isolated change.
