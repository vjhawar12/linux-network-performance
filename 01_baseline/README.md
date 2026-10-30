# Phase A — Baseline Network Benchmarking

**Status: Python prototype under development — no validated test runs or switch-performance measurements.** Phase A aims to establish an end-to-end measurement harness using Python, `iperf3`, rtnetlink NIC counters, and eventually driver-specific/switch-port statistics. It is *not* a custom kernel packet generator.

## Objective

For a verified physical path from a source NIC port through the switch to a destination NIC port:

1. Generate reproducible UDP traffic at specified offered bitrates and durations.
2. Report **receiver-side iperf3 throughput, UDP loss, and jitter**.
3. Snapshot TX-interface and RX-interface counters **before and after** each run.
4. Record configuration, results, and errors so multiple rates/packet sizes can be compared.
5. Distinguish evidence from host-side processing, NIC counters, and (later) switch-port statistics without falsely equating them.

## Intended test path

```text
Same Linux host (default netns)         Same Linux host (rxbench netns)
iperf3 client                           iperf3 server
    |                                       ^
Intel X710 port 0                       Intel X710 port 1
enp23s0f0np0                            enp23s0f1np1
    |                                       ^
    +--> external switch ingress --> egress --------+
```

| Setting | Proposed baseline | Verification needed |
|---|---|---|
| TX device | `enp23s0f0np0` | Confirm physical external switch connection |
| RX device | `enp23s0f1np1` | Confirm physical external switch connection |
| TX namespace | Default namespace | Ensure route selects intended port |
| RX namespace | `rxbench` | Create only after safety checks |
| TX IP | `172.31.250.1/30` | Subnet not yet approved/configured |
| RX IP | `172.31.250.2/30` | Subnet not yet approved/configured |
| Link rate | Both ports report 10 Gb/s | Confirm under test and record settings |

**Why a separate RX namespace?** If both addresses belong to the same host network namespace, Linux can route the destination IP locally instead of transmitting through the switch. Namespace separation allows independent routing/socket state. It does **not** replace physical cable, VLAN, or packet-path verification.

The separate-adapter alternative (X710 TX → X722 RX) remains a *future profile*, contingent on suitable switch connectivity and physical media.

## Requirements (development)

- Linux with access to the intended Ethernet NICs; `ip` from iproute2, `ethtool`, `iperf3`.
- Python, `psutil`, and `pyroute2`; namespace setup and Netlink collection require corrections before running.
- Permissions to inspect the required NIC counters. Network setup, namespaces, and server startup can require elevated privileges.
- An iperf3 server bound to the intended receiver IP *inside* the receiver namespace once it has been configured.

**Do not use a management interface or management IP as the test destination.** Traffic to another address on the same host does not by itself demonstrate a path through the external switch.

### Read-only discovery (before network changes)

```bash
ethtool -i enp23s0f0np0
ethtool enp23s0f0np0
ethtool -i enp23s0f1np1
ethtool enp23s0f1np1
ip -br address
ip netns list
ethtool -S enp23s0f0np0
```

Confirm switch-port mapping and that neither candidate interface is serving a production connection. Check the VLANs, subnets, and network manager before creating namespaces or assigning addresses.

## Intended CLI (after current bugs are fixed)

```bash
python3 01_baseline/benchmark.py \
    enp23s0f0np0 enp23s0f1np1 \
    --tx-ip 172.31.250.1 \
    --rx-ip 172.31.250.2 \
    --rx-namespace rxbench \
    --bitrate 1G \
    --duration 10
```

**Design example only — do not run this command as a validated quick-start.** The example IPs are unapproved placeholders. The source contains an `iperf3` server launcher, but it is not yet correctly integrated, checked, or cleaned up. Do not change networking or generate traffic before verifying management safety, routes, cabling, and switch forwarding.

## Measurements

| Layer / source | Planned data | Interpretation |
|---|---|---|
| TX rtnetlink `IFLA_STATS64` | `tx_packets`, `tx_bytes`, `tx_dropped`, `tx_errors` | Sender interface/driver view |
| RX rtnetlink `IFLA_STATS64` | `rx_packets`, `rx_bytes`, `rx_dropped`, `rx_errors`, `rx_crc_errors`, `rx_missed_errors` | Receiver interface/driver view |
| iperf3 JSON | Sender/receiver bitrate, UDP packets/loss, jitter | End-to-end application view |
| `ethtool -S` (later) | Device/port/queue-specific diagnostics | Driver-specific evidence |
| external switch counters (later) | Ingress, egress, drops/errors | Switch-side evidence |

`/sys/class/net/.../statistics` values are cumulative; use deltas. They are kernel-reported standard statistics, not necessarily one fresh hardware register read per virtual file. Background control traffic may contribute to packet/byte counters. `rx_errors` is an aggregate and is **not synonymous with CRC errors**. `rx_missed_errors` and `rx_dropped` measure different conditions. A receiver-side iperf3 UDP loss report is not a substitute for hardware error counters.

## Current implementation and verification status

[benchmark.py](benchmark.py) is the **only implemented benchmark source**. The current code includes argument parsing, `psutil` NIC discovery, a proposed namespace setup function, an `iperf3` client/server orchestration outline, two threaded stats pollers, and timestamped `IFLA_STATS64` counter snapshots. This is **implemented source code, not yet validated behavior**.

### Known blockers at current HEAD

1. **Namespace initialization order:** `main` looks up the RX interface inside `rxbench` before creating/moving it, then calls undefined `create_rx_namespace()`. The existing `setup_network()` helper is not invoked.
2. **Namespace-aware networking:** code uses `IPRoute(netns=...)` rather than a correctly scoped `NetNS` handle. After moving an interface, configuration must occur in its new namespace; interface indices are namespace-local. Setup currently uses /24 prefixes although the design proposes /30 addresses.
3. **Traffic/report lifecycle:** `launch_server()` starts an `iperf3` process but doesn't check readiness or clean it up. The client subprocess result is not checked before parsing; the parser assumes `end.sum` and several UDP fields exist without handling sender/receiver schemas or errors.
4. **Statistics reliability:** collection threads start without a first-sample barrier; baseline snapshots may be empty. Missing `IFLA_STATS64` and counters are not handled, and exceptions may leave the threads/server or namespace in an inconsistent state.
5. **Compatibility and validation:** current summary f-strings require Python 3.12+ syntax; no unit/integration tests, verified physical path, saved result files, or repeatable throughput measurements exist.

These are unresolved development issues, **not published performance findings**. Do not run the network-setup prototype on a shared or production host without first reviewing interface ownership, management access, routing, and cleanup.

### What the current collector is intended to measure

- Two polling threads request `IFLA_STATS64` through Netlink for TX and RX interface counters, rather than directly reading `/sys/class/net/<interface>/statistics/`.
- The tool intends to compare **counter deltas** over a controlled run and present them beside receiver-side UDP `iperf3` measurements.
- These observations cover different layers. UDP application loss, driver/NIC `rx_dropped`, CRC errors, and external switch drops are **not interchangeable**; link status alone does not prove traffic passed through the external switch.

## Next implementation checkpoints

1. **Correctness and unit tests:** Resolve the undefined namespace creation call, initialize namespaces/interfaces in the correct order, use `NetNS` for namespace-specific Netlink work, and handle iperf3 failures/JSON schemas.
2. **Trustworthy statistics:** Add first-sample synchronization, safe per-interface counter collection across namespaces, missing-counter handling, and thread/process cleanup.
3. **Safe preflight:** Confirm interfaces, namespaces, assigned addresses, link status, selected route, and receiver availability. Add a non-transmitting dry-run.
4. **Results:** Save configuration + iperf3 sender/receiver data + TX/RX deltas as JSON with timestamps and error status.
5. **Test coverage:** Unit tests for deltas, counter resets/missing data, subprocess failures, and representative iperf3 JSON responses.
6. **Hardware integration:** Confirm cables, switch forwarding/VLANs, and subnet approval; verify ARP/ICMP/TX/RX counters across physical ports at low rate.
7. **Benchmark matrix:** Repeat runs at increasing load and varying UDP payload sizes; then examine reverse/bidirectional traffic and driver/switch-specific counters.

## Definition of done

Phase A is complete when a known TX-to-switch-to-RX physical path is confirmed; the runner reliably produces receiver-side iperf3 measurements and per-NIC counter deltas; failures are reported clearly; results are saved with reproducible settings; and multiple controlled runs can be compared. Achieving 10 Gb/s is a **measurement goal**, not a prerequisite for declaring the test tool functional.
