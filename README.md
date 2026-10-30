# Linux Network Performance

A staged Linux networking project for characterizing high-speed Ethernet traffic, Linux host/NIC behavior, and performance bottlenecks. The proposed experiment is a **10 GbE host–switch–host forwarding path through an external Ethernet switch test board**. This is distinct from the separate SAMV71/LAN9693 firmware-porting work.

> **Status (October 2026):** Phase A is an **in-development Python prototype**, not a working or validated benchmark. The repository contains no verified switch-throughput, packet-loss, or 10 Gb/s performance results. Phases B–E are design roadmaps only.

## What's implemented today

- Python CLI options for two NIC interfaces, destination/source IPs, namespace, offered bitrate and duration.
- Initial UDP `iperf3` subprocess integration and JSON reporting logic (needs error-handling/schema fixes).
- A threaded TX/RX statistics polling design using `pyroute2`/rtnetlink `IFLA_STATS64`, snapshots, and counter deltas.
- Host interface inspection with `psutil` and sysfs; preliminary network-namespace setup and server launch helpers.

**Not yet complete:** safe namespace creation/configuration, end-to-end execution, verified physical traffic through the switch, error handling, automated tests, saved reports, and repeatable performance measurements. The source is **not a ready-to-run benchmark**; see [Phase A blockers](01_baseline/README.md#known-blockers-at-current-head).

## Test topology

The intended baseline uses **two distinct physical Ethernet ports**, potentially on the same multiport NIC:

```text
             One Linux x86 host
    +----------------------------------+
    | iperf3 client     iperf3 server  |
    | Default netns     Receiver netns |
    |      |                  ^        |
    |  TX interface       RX interface |
    +------|------------------|--------+
           |                  |
           v                  |
     Switch ingress --> Switch egress
         (external switch forwarding)
```

The current host has an **Intel X710 four-port 10 GbE SFP+ controller** (Linux `i40e` driver) and **Intel X722 10GBASE-T interfaces**. A proposed same-adapter baseline uses X710 ports `enp23s0f0np0` (TX) and `enp23s0f1np1` (RX). Their 10 Gb/s link status has been observed, but **physical cable-to-switch-port mapping is not yet confirmed**.

Because both test ports reside on the same host, assigning two IP addresses in the *same* network namespace can lead to local kernel delivery instead of packets traversing the switch. The plan is to put the receiver interface and iperf3 server in a separate network namespace and verify traffic on both physical NICs. A cross-adapter profile can be added later if the X722 cabling/media and switch ports support it.

The example addresses `172.31.250.1/30` and `172.31.250.2/30` are **proposed test values, not provisioned or approved subnets**. Do not apply them without checking the existing host networking and confirming the physical path.

## Roadmap

| Directory | Phase | Objective | Status |
|---|---|---|---|
| [01_baseline](01_baseline/README.md) | A — Linux tools + Python | Establish iperf3 throughput/loss and NIC-counter baselines | **In progress** |
| [02_kernel_pktgen](02_kernel_pktgen/README.md) | B — Kernel pktgen | Compare in-kernel traffic generation and packet-rate limits | Planned |
| [03_socket_engine](03_socket_engine/README.md) | C — Socket engine | Implement a controlled C traffic generator/receiver | Planned |
| [04_kernel_tracing](04_kernel_tracing/README.md) | D — Kernel tracing | Diagnose IRQ, softirq, queue, and drop behavior | Planned |
| [05_advanced_io](05_advanced_io/README.md) | E — Advanced I/O | Evaluate AF_XDP/DPDK if earlier measurements justify it | Optional / planned |

## Measurement model

A useful benchmark combines **multiple independent observation points**:

- **Application:** iperf3 sender/receiver bitrate, UDP sequence-based loss, jitter, duration.
- **Host network interface:** `/sys/class/net/<interface>/statistics/` counters such as TX/RX packets, bytes, drops, and errors.
- **NIC/driver:** `ethtool -S <interface>` (e.g., Intel `i40e` per-queue and port counters).
- **Switch:** external switch ingress/egress counters if available (future integration).

An iperf3 loss number is not equivalent to NIC `rx_dropped`; a zero NIC error count does not prove the external switch never discarded packets. Counters are cumulative and must be compared before and after a controlled run. Background traffic and different hardware accounting points can affect comparisons.

## Safety and reproducibility

- Confirm cabling, VLAN/forwarding configuration, management connectivity, and subnet availability **before** modifying interface addresses or network namespaces.
- Keep privileged network setup separate from the benchmark runner. Avoid moving a management interface into a namespace.
- Begin with low offered load and verify the actual physical path; increase rate and vary packet size only after validation.
- Record test direction, interface names, adapter/driver, link speed, packet size, target bitrate, duration, and counter deltas.
- Test results represent the complete **host–NIC–switch–NIC–host** path. Host CPU, PCIe, driver, and receive processing may become bottlenecks.

See [Phase A](01_baseline/README.md) for the current implementation and its outstanding fixes.

## Current code

`01_baseline/benchmark.py` is the only implemented benchmark source at present. Planned local requirements include Linux, Python, `iperf3`, `iproute2`, `ethtool`, and Python packages `psutil` and `pyroute2` (the current script imports them). Namespace setup, counter initialization, and iperf3 reporting require fixes **before it can be treated as runnable or used for measurements**. The code presently cannot complete the described benchmark end-to-end.
