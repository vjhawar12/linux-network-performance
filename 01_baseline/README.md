# Stage 1 — Python + iperf3 Baseline

**Status (16 November 2026).** The Python runner completed virtual Ethernet smoke tests at 100 Mb/s, 1 Gb/s, and 10 Gb/s UDP target rates. Physical 10 GbE switch testing is planned. This stage provides a UDP/IP reference for [AF_PACKET](../02_af_packet/README.md), [AF_XDP](../03_af_xdp/README.md), and [DPDK](../04_dpdk/README.md).

## Virtual Ethernet smoke tests

All three trials ran on one Linux host. The iperf3 client sent from `veth-tx02` in the default network namespace to its virtual peer `veth-rx02` in `rxbench-veth-02`. The veth pair forwards traffic inside the Linux kernel. The traffic path used no physical NIC, PHY, cable, or external switch. Each trial used Python 3.12 and a 10 s test duration.

| Metric | 100 Mb/s UDP target | 1 Gb/s UDP target | 10 Gb/s UDP target |
| --- | ---: | ---: | ---: |
| iperf3 reported UDP bitrate | 99.995 Mb/s | 979.982 Mb/s | 1,061.699 Mb/s |
| iperf3 reported UDP loss | 0.126% | 0.316% | 0.653% |
| iperf3 reported UDP jitter | 0.130 ms | 0.022 ms | 0.013 ms |
| Interface drops and errors | 0 reported on TX and RX | 0 reported on TX and RX | 0 reported on TX and RX |
| Median gap between logged snapshots (TX / RX) | 10.002 / 9.999 ms | 10.000 / 10.000 ms | 9.999 / 10.000 ms |
| Longest gap between logged snapshots (TX / RX) | 63.932 / 62.982 ms | 34.962 / 38.537 ms | 32.290 / 34.122 ms |

Bitrate, loss, jitter, and interface counters come from the terminal summaries. The script reads iperf3's `end.sum` field, and snapshot gaps use adjacent timestamps in the TX and RX logs. The 10 Gb/s target produced about 1.062 Gb/s in this run. The capture does not identify what limited the achieved rate.

iperf3 estimates UDP loss from sequence gaps at the receiving application. The Linux `tx_dropped` and `rx_dropped` counters track drops at the interfaces. A datagram can reach the RX interface and be discarded later if the UDP socket's receive buffer is full. The captures contain no UDP socket counters, so the loss location is unknown. The independent TX and RX snapshot boundaries also make the packet-total difference unsuitable for loss accounting. On a rerun, compare `UdpRcvbufErrors` before and after the test in the RX namespace, before the launcher removes it. [iperf3 UDP loss accounting](https://github.com/esnet/iperf/discussions/1680) · [Linux interface counter definitions](https://docs.kernel.org/networking/statistics.html#struct-rtnl-link-stats64)

| UDP target | Terminal summary | Counter logs | Screenshot |
| --- | --- | --- | --- |
| 100 Mb/s | [Summary](results/veth-100m-xSc6uQ/summary.txt) | [TX](results/veth-100m-xSc6uQ/tx.txt) · [RX](results/veth-100m-xSc6uQ/rx.txt) | [100 Mb/s veth smoke test](../screenshots/veth-smoke-tests/01_baseline_100_mbps.png) |
| 1 Gb/s | [Summary](results/veth-100m-2Nskcn/summary.txt) | [TX](results/veth-100m-2Nskcn/tx.txt) · [RX](results/veth-100m-2Nskcn/rx.txt) | [1 Gb/s veth smoke test](../screenshots/veth-smoke-tests/01_baseline_1_gbps.png) |
| 10 Gb/s | [Summary](results/veth-100m-OkUuRY/summary.txt) | [TX](results/veth-100m-OkUuRY/tx.txt) · [RX](results/veth-100m-OkUuRY/rx.txt) | [10 Gb/s target veth smoke test](../screenshots/veth-smoke-tests/01_baseline_10gbps.png) |

### Reproduce the smoke tests

Install Python 3.12 with venv support, `iperf3`, and `iproute2` on a Linux host. The launcher uses `sudo` to create the veth pair and configure the RX namespace. From the repository root:

```bash
cd 01_baseline
python3.12 -m venv .venv312
.venv312/bin/python3 -m pip install psutil pyroute2
./run.sh
```

[`run.sh`](run.sh) uses fixed interface names and the `172.31.251.0/24` test subnet. It removes leftover interfaces with those names and recreates the named RX namespace, so reserve the names for this test and run one instance at a time. Python 3.12's Linux `fork` default lets workers inherit the current script's stop event. Python 3.14's `forkserver` default requires passing that event explicitly to the workers.

The checked-in launcher uses `--bitrate 10G --duration 10`. Set `--bitrate 100M` or `--bitrate 1G` in `run.sh` for the lower-rate trials. All three runs use the launcher's `results/veth-100m-*/` directory prefix and write `summary.txt`, `tx.txt`, and `rx.txt`. The launcher prints a `tail -F` command to follow the logs in batches about once per second.

## Planned physical 10 GbE test

The first planned hardware experiment will measure traffic, drops, errors, and timing through two physical NIC ports connected by an external switch.

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

The runner uses the iperf3 executable for traffic generation and measurement. Reusable orchestration and telemetry can move into [`common/`](../common/README.md) as additional backends mature.

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

**Example command for the planned physical test.** Use dedicated interfaces and addresses assigned to the test.

```bash
sudo 01_baseline/.venv312/bin/python3 01_baseline/benchmark.py \
  enp23s0f0np0 enp23s0f1np1 \
  --tx-ip 172.31.250.1 --rx-ip 172.31.250.2 \
  --rx-namespace rxbench --bitrate 1G --duration 10
```

The script assigns /24 addresses and can remove an existing namespace named by the CLI. Use a dedicated test namespace and interfaces.

## Measurement and lifecycle limitations

1. **Startup order.** The `namespace_created` event is set after creating the namespace, *before* moving and configuring the RX NIC. A full-interface-ready event, verified listening server, and failed-worker detection are still needed.
2. **First valid sample.** The stream can send initial zero-valued telemetry before the first `IFLA_STATS64` acquisition completes.
3. **End-of-test boundary.** Synchronous event waits inside `async def` can block monitors. The last buffered sample can predate the end of traffic.
4. **Error and cleanup handling.** Add bounded startup waits, worker failure reporting, and explicit handling of iperf3 errors. Snapshot buffers grow throughout the run.
5. **Timing.** Add monotonic timestamps at each counter read to measure collection intervals and missed deadlines independently of snapshot serialization.

## Measurement plan

Collect application UDP throughput, lost datagrams and jitter; Linux NIC counters such as `tx_packets`, `rx_packets`, `tx_dropped`, `rx_dropped`, `rx_errors`, `rx_crc_errors`, and `rx_missed_errors`; link events; and `ethtool -S` / switch-port counters when available.

Counter semantics vary by layer. iperf3 receiver loss is not the same metric as hardware CRC errors or NIC `rx_dropped`. The kernel and NIC may update counters less frequently than polling occurs. Record actual collection timestamps and the offered payload rate versus Ethernet line rate. Integrate physical shaker-table telemetry later for precise mechanical correlation.

## Stage 1 exit criteria

- The 100 Mb/s, 1 Gb/s, and 10 Gb/s target veth smoke tests have summaries and TX/RX logs. Repeat the trials and check startup, error handling, and shutdown across runs.
- A known switch path is checked physically, not assumed.
- iperf3 completes, and validated before/after counter samples plus reproducible test settings are saved.
- Errors and shutdown do not strand the child processes or damage unrelated network configuration.
- Repeatable tests provide a baseline for [custom raw Ethernet](../02_af_packet/README.md).

Once functional, freeze a known Stage 1 revision for future comparisons. More elaborate error recovery or memory/performance optimization can follow observed problems; basic safety and sample correctness cannot.
