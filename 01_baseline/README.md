# Phase A — Baseline Network Benchmarking

**Status: work in progress (not yet validated).** This phase uses Python, iperf3, Linux interface statistics, and eventually driver-specific counters to create a repeatable baseline for traffic through the LAN9693. It is intentionally a *measurement harness*, not a custom kernel packet generator.

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
    +--> LAN9693 ingress --> egress --------+
```

| Setting | Proposed baseline | Verification needed |
|---|---|---|
| TX device | `enp23s0f0np0` | Confirm physical LAN9693 connection |
| RX device | `enp23s0f1np1` | Confirm physical LAN9693 connection |
| TX namespace | Default namespace | Ensure route selects intended port |
| RX namespace | `rxbench` | Create only after safety checks |
| TX IP | `172.31.250.1/30` | Subnet not yet approved/configured |
| RX IP | `172.31.250.2/30` | Subnet not yet approved/configured |
| Link rate | Both ports report 10 Gb/s | Confirm under test and record settings |

**Why a separate RX namespace?** If both addresses belong to the same host network namespace, Linux can route the destination IP locally instead of transmitting through the switch. Namespace separation allows independent routing/socket state. It does **not** replace physical cable, VLAN, or packet-path verification.

The separate-adapter alternative (X710 TX → X722 RX) remains a *future profile*, contingent on suitable switch connectivity and physical media.

## Requirements (development)

- Linux with access to the intended Ethernet NICs; `ip` from iproute2, `ethtool`, `iperf3`.
- Python and `psutil`; the current script also imports `pyroute2`, although its namespace handling is **not working yet**.
- Permissions to inspect the required NIC counters. Network setup, namespaces, and server startup can require elevated privileges.
- An iperf3 server bound to the intended receiver IP *inside* the receiver namespace once it has been configured.

**Do not benchmark against the server's existing management address** (`10.1.10.106` was observed on `eno2np1`); that address is on the same machine and does not establish the LAN9693 test path.

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

**This is a target invocation, not a working quick-start today.** The example IPs are placeholders until the test network is approved. A receiver-side iperf3 server must already exist; the current script does not start it. Do not run high-rate tests before confirming the route and physical switch path.

## Measurements

| Layer / source | Planned data | Interpretation |
|---|---|---|
| TX sysfs | `tx_packets`, `tx_bytes`, `tx_dropped`, `tx_errors` | Sender NIC/driver view |
| RX sysfs | `rx_packets`, `rx_bytes`, `rx_dropped`, `rx_errors`, `rx_crc_errors`, `rx_missed_errors` | Receiver NIC/driver view |
| iperf3 JSON | Sender/receiver bitrate, UDP packets/loss, jitter | End-to-end application view |
| `ethtool -S` (later) | Device/port/queue-specific diagnostics | Driver-specific evidence |
| LAN9693 counters (later) | Ingress, egress, drops/errors | Switch-side evidence |

`/sys/class/net/.../statistics` values are cumulative; use deltas. They are kernel-reported standard statistics, not necessarily one fresh hardware register read per virtual file. Background control traffic may contribute to packet/byte counters. `rx_errors` is an aggregate and is **not synonymous with CRC errors**. `rx_missed_errors` and `rx_dropped` measure different conditions. A receiver-side iperf3 UDP loss report is not a substitute for hardware error counters.

## Current source and known issues

The only implementation is [benchmark.py](benchmark.py). It already contains the CLI outline, TX/RX counter selection, interface inspection, traffic generation, and delta calculations. **The current commit has bugs and cannot yet produce trustworthy results:**

- `DEFAULT_NAMESPACE` is undefined; `get_nic_stats()` requires a namespace argument, but the caller omits it.
- `NetNS("myns")` is hardcoded and does **not** make ordinary `Path` reads execute in that namespace; the current code also attempts to `open()` a statistics directory.
- `generate_traffic()` uses an undefined `server_ip` and passes an integer `duration` directly to `subprocess.run` command arguments.
- The iperf3 JSON parser assumes `end.sum` and accesses loss/jitter fields without checking the actual report structure or subprocess errors; receiver-specific summaries should be validated against the installed iperf3 version.
- Output uses undefined `bits_ps`/`jitter_ms` variables and contains an f-string quoting issue on Python versions before 3.12.
- `get_intf_info()` sees only the calling process's network namespace, so it needs special treatment for a moved RX interface.
- No preflight path verification, dry-run, saved results, or automated tests yet.

Avoid treating the current script as a production/validated benchmark. Use synthetic statistics and saved iperf3 JSON samples to develop the parser and delta calculations before deploying the network setup.

## Next implementation checkpoints

1. **Correctness:** Fix the undefined names, function signatures, subprocess argument types, JSON parsing, and output formatting.
2. **Namespace-aware collection:** Implement RX statistics retrieval in the designated namespace (e.g., a helper invoked with `ip netns exec`), without switching the benchmark process itself.
3. **Safe preflight:** Confirm interfaces, namespaces, assigned addresses, link status, selected route, and receiver availability. Add a non-transmitting dry-run.
4. **Results:** Save configuration + iperf3 sender/receiver data + TX/RX deltas as JSON with timestamps and error status.
5. **Test coverage:** Unit tests for deltas, counter resets/missing data, subprocess failures, and representative iperf3 JSON responses.
6. **Hardware integration:** Confirm cables, switch forwarding/VLANs, and subnet approval; verify ARP/ICMP/TX/RX counters across physical ports at low rate.
7. **Benchmark matrix:** Repeat runs at increasing load and varying UDP payload sizes; then examine reverse/bidirectional traffic and driver/switch-specific counters.

## Definition of done

Phase A is complete when a known TX-to-switch-to-RX physical path is confirmed; the runner reliably produces receiver-side iperf3 measurements and per-NIC counter deltas; failures are reported clearly; results are saved with reproducible settings; and multiple controlled runs can be compared. Achieving 10 Gb/s is a **measurement goal**, not a prerequisite for declaring the test tool functional.
