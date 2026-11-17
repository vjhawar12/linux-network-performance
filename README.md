# Linux Network Performance

This project compares Linux packet I/O approaches for stress-testing Ethernet NICs, physical links, and switch forwarding. The goal is to validate whether a switch and its PHYs can sustain near-line-rate traffic during shaker-table vibration tests, starting at 10 GbE and progressing to 100 GbE.

Two NIC ports on one Linux host send and receive traffic through the external switch. A static run must first show that the host can generate and verify the intended workload with no observed host-side packet loss. That baseline gives the vibration runs a reference for separating host bottlenecks from switch or physical-link faults.

## Packet I/O roadmap

| Stage | What it tests | Status |
| --- | --- | --- |
| [1. UDP/IP baseline](01_baseline/README.md) | Establish a reference with Python, iperf3, and Linux interface counters. | Virtual Ethernet smoke tests recorded. Physical 10 GbE testing is planned. |
| [2. Raw Ethernet](02_af_packet/README.md) | Use C++ and `AF_PACKET` to send and receive Layer 2 test frames and compare packet rate, loss, and CPU usage with the UDP baseline. | Experimental sender and receiver code. End-to-end validation is in progress. |
| [3. AF_XDP](03_af_xdp/README.md) | Evaluate shared buffers, descriptor rings, and zero-copy where the NIC and driver support it. | Planned. |
| [4. DPDK](04_dpdk/README.md) | Evaluate userspace polling and scaling across NIC queues and CPU cores. | Planned. |

The [shared architecture](common/README.md) uses Python for experiment orchestration, telemetry, and reporting, with C++ handling per-frame work in Stages 2–4. [Kernel pktgen](diagnostics/kernel_pktgen/README.md) provides a planned independent packet-rate reference, and [kernel tracing](diagnostics/kernel_tracing/README.md) supports bottleneck investigation.

## How tests are compared

- Use comparable frame sizes, offered loads, and test durations. Report achieved receive throughput, frames per second, loss, and CPU usage. Include Ethernet wire overhead when comparing against line rate.
- Cross-check receiver sequence gaps against host, NIC, and switch-port counters to locate drops. Log actual sampling timestamps and correlate them with shaker-table telemetry when available.
- Verify the physical forwarding path and repeat the static baseline before testing under vibration. Use isolated test interfaces and switch ports for high-rate traffic.

In one [unpaced Stage 1 veth trial](01_baseline/README.md#unpaced-packet-rate), iperf3 reported **7.387 Gb/s aggregate UDP bitrate** and **35.614% UDP loss**, while both virtual interfaces reported zero drops. UDP socket receive-queue overflow is a plausible cause. The saved run did not capture the RX namespace’s `UdpRcvbufErrors` counter, which can show whether datagrams were dropped at that queue. The next milestone is a verified physical 10 GbE baseline through the switch.
