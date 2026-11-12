# Stage 1 — Python + iperf3 Baseline

**Status: implemented prototype; no validated end-to-end switch test yet.** Stage 1 establishes the reference experiment before we build the custom C++ traffic engines in [Stage 2](../02_af_packet/README.md), [Stage 3](../03_af_xdp/README.md), and [Stage 4](../04_dpdk/README.md). The Python runner and monitoring logic should remain available as a comparison baseline.

## Goal and topology

Measure traffic, drops, errors and timing through an actual TX NIC -> external switch -> RX NIC forwarding path, initially with UDP `iperf3` at 10 GbE.

```text
Linux host / default netns         Same host / rxbench netns
        iperf3 client                    iperf3 server
             |                                ^
           TX NIC                           RX NIC
             |                                ^
             +--> external switch -----------+
```

Candidate ports: Intel X710 `enp23s0f0np0` and `enp23s0f1np1`. Confirm physical cabling, switch forwarding, interface ownership, and addressing before attempting an experiment. A separate RX namespace prevents unintended local IP delivery when both NICs belong to the same machine.

## Current code

The existing [`benchmark.py`](benchmark.py) includes:

- Python CLI for interface names, addressing, RX namespace, bitrate, duration and output files.
- One process per TX/RX interface, with worker threads for `IFLA_STATS64` polling and rtnetlink link-status events.
- A nominal 10 ms monotonic-deadline timer for polling/streaming; worker snapshots are transferred through `multiprocessing.Pipe`.
- Asyncio monitor tasks that drain telemetry from the pipes, buffer samples and periodically append them to disk.
- UDP iperf3 client/server orchestration and initial/final interface counter-delta calculation.
- Events for namespace creation and stream lifecycle.

The old, unused C/C++ interface-monitoring prototype, queue, and scheduler were removed from this directory, along with their obsolete root CMake target. **This stage is Python + iperf3 only.** Stages 2–4 are separate future C++ backends; reusable Python code will be extracted into [`common/`](../common/README.md) after Stage 1 works.

## Requirements and safety

Linux, Python, `psutil`, `pyroute2`, `iperf3`, `iproute2`, `ethtool`, and appropriate permissions for interface/network-namespace configuration.

Read-only checks:

```bash
ip -br address
ip netns list
ethtool -i enp23s0f0np0
ethtool -i enp23s0f1np1
ethtool -S enp23s0f0np0
```

**Intended CLI, not a validated quick-start** (use only approved interfaces/addresses):

```bash
python3 01_baseline/benchmark.py \
  enp23s0f0np0 enp23s0f1np1 \
  --tx-ip 172.31.250.1 --rx-ip 172.31.250.2 \
  --rx-namespace rxbench --bitrate 1G --duration 10
```

The current script configures addresses using **/24**, whereas the earlier test plan specified **/30**; reconcile before repeatable measurements. The script can remove an existing namespace named by the CLI; do not use a namespace or NIC owned by another program or your management network.

## Known gaps before trusting a benchmark

1. **Startup order:** the `namespace_created` event is set after creating the namespace, *before* moving and configuring the RX NIC. A full-interface-ready event, verified listening server, and failed-worker detection are still needed.
2. **First valid sample:** the stream can send initial zero-valued telemetry before the first `IFLA_STATS64` acquisition completes.
3. **End-of-test boundary:** synchronous event waits inside `async def` can block monitors; the last buffered sample may predate the traffic end because a final collection/pipe-drain handshake is absent.
4. **Error/cleanup:** iperf3 subprocess exit codes/JSON failures, worker failure handling, timeout/cleanup, and namespace ownership are not yet fully hardened. Buffers grow through a test session.
5. **Timing:** 10 ms is a target interval, not real-time determinism. Measure actual collection timing and missed deadlines. Timestamp counter acquisition instead of only cached snapshot serialization.

These are known limitations of the prototype, **not** evidence of hardware loss or throughput.

## Measurement plan

Collect application UDP throughput, lost datagrams and jitter; Linux NIC counters such as `tx_packets`, `rx_packets`, `tx_dropped`, `rx_dropped`, `rx_errors`, `rx_crc_errors`, and `rx_missed_errors`; link events; and `ethtool -S` / switch-port counters when available.

Counter semantics vary by layer. iperf3 receiver loss is not the same metric as hardware CRC errors or NIC `rx_dropped`. The kernel and NIC may update counters less frequently than polling occurs. Record actual collection timestamps and the offered payload rate versus Ethernet line rate. Integrate physical shaker-table telemetry later for precise mechanical correlation.

## Stage 1 exit criteria

- A disposable `veth` test or safe equivalent verifies initialization and the full process/IPC lifecycle.
- A known switch path is checked physically, not assumed.
- iperf3 completes, and validated before/after counter samples plus reproducible test settings are saved.
- Errors and shutdown do not strand the child processes or damage unrelated network configuration.
- Repeatable tests provide a baseline for [custom raw Ethernet](../02_af_packet/README.md).

Once functional, freeze a known Stage 1 revision for future comparisons. More elaborate error recovery or memory/performance optimization can follow observed problems; basic safety and sample correctness cannot.
