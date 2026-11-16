# Stage 1 — Python + iperf3 Baseline

**Status (16 November 2026).** The Python runner completed virtual Ethernet smoke tests at aggregate UDP targets of 100 Mb/s, 1 Gb/s, 3 Gb/s, and 10 Gb/s. The 10 Gb/s target was tested with one and four streams, and unpaced trials used four and ten streams. Physical 10 GbE switch testing is planned. This stage provides a UDP/IP reference for [AF_PACKET](../02_af_packet/README.md), [AF_XDP](../03_af_xdp/README.md), and [DPDK](../04_dpdk/README.md).

## Virtual Ethernet smoke tests

The bitrate-targeted and unpaced trials ran on one Linux host. The iperf3 client sent from `veth-tx02` in the default network namespace to its virtual peer `veth-rx02` in `rxbench-veth-02`. The veth pair forwards traffic inside the Linux kernel. The traffic path used no physical NIC, PHY, cable, or external switch. Each trial used Python 3.12 and a 10 s test duration.

| Metric | 100 Mb/s, 1 stream | 1 Gb/s, 1 stream | 10 Gb/s, 1 stream | 10 Gb/s, 4 streams |
| --- | ---: | ---: | ---: | ---: |
| Configured UDP target per stream | 100 Mb/s | 1 Gb/s | 10 Gb/s | 2.5 Gb/s |
| iperf3 reported aggregate UDP bitrate | 99.995 Mb/s | 979.982 Mb/s | 1,061.699 Mb/s | 2,604.348 Mb/s |
| iperf3 reported UDP loss | 0.126% | 0.316% | 0.653% | 0.171% |
| iperf3 reported UDP jitter | 0.130 ms | 0.022 ms | 0.013 ms | 0.023 ms |
| Interface drops and errors | 0 on TX and RX | 0 on TX and RX | 0 on TX and RX | 0 on TX and RX |
| Median gap between logged snapshots (TX / RX) | 10.002 / 9.999 ms | 10.000 / 10.000 ms | 9.999 / 10.000 ms | 10.000 / 10.000 ms |
| Longest gap between logged snapshots (TX / RX) | 63.932 / 62.982 ms | 34.962 / 38.537 ms | 32.290 / 34.122 ms | 50.781 / 42.418 ms |

Bitrate, loss, jitter, and interface counters come from the terminal summaries. The script reads iperf3's `end.sum` field, and snapshot gaps use adjacent timestamps in the TX and RX logs. At the same 10 Gb/s aggregate target, the four-stream smoke test reported 2.45× the bitrate of the single-stream smoke test and reached 26.0% of the target. The saved summaries contain aggregate measurements. Separate sender, receiver, and per-stream rates require the full iperf3 report.

iperf3 estimates UDP loss from sequence gaps at the receiving application. The Linux `tx_dropped` and `rx_dropped` counters track drops at the interfaces. A datagram can reach the RX interface and be discarded later if the UDP socket's receive buffer is full. The captures contain no UDP socket counters, so the loss location is unknown. The independent TX and RX snapshot boundaries also make the packet-total difference unsuitable for loss accounting. On a rerun, compare `UdpRcvbufErrors` before and after the test in the RX namespace, before the launcher removes it. [iperf3 UDP loss accounting](https://github.com/esnet/iperf/discussions/1680) · [Linux interface counter definitions](https://docs.kernel.org/networking/statistics.html#struct-rtnl-link-stats64)

| Aggregate UDP target and stream count | Terminal summary | Counter logs | Screenshot |
| --- | --- | --- | --- |
| 100 Mb/s, 1 stream | [Summary](results/veth-100mbps-target-1-stream/summary.txt) | [TX](results/veth-100mbps-target-1-stream/tx.txt) · [RX](results/veth-100mbps-target-1-stream/rx.txt) | [100 Mb/s veth smoke test](../screenshots/veth-smoke-tests/01_baseline_100_mbps.png) |
| 1 Gb/s, 1 stream | [Summary](results/veth-1gbps-target-1-stream/summary.txt) | [TX](results/veth-1gbps-target-1-stream/tx.txt) · [RX](results/veth-1gbps-target-1-stream/rx.txt) | [1 Gb/s veth smoke test](../screenshots/veth-smoke-tests/01_baseline_1_gbps.png) |
| 10 Gb/s, 1 stream | [Summary](results/veth-10gbps-target-1-stream/summary.txt) | [TX](results/veth-10gbps-target-1-stream/tx.txt) · [RX](results/veth-10gbps-target-1-stream/rx.txt) | [Single-stream smoke test](../screenshots/veth-smoke-tests/01_baseline_10gbps.png) |
| 10 Gb/s, 4 streams | [Summary](results/veth-10gbps-target-4-streams/summary.txt) | [TX](results/veth-10gbps-target-4-streams/tx.txt) · [RX](results/veth-10gbps-target-4-streams/rx.txt) | [Four-stream smoke test](../screenshots/veth-smoke-tests/01_baseline_10gbps_perstream.png) |

### Paced ten-stream trial

One 10-second veth run used `-P 10 -b 300M`, for a 3 Gb/s aggregate UDP target. iperf3 applies `-b` per stream. [iperf3 options](https://software.es.net/iperf/invoking.html)

| Aggregate UDP target | iperf3 reported aggregate UDP bitrate | UDP loss | UDP jitter | Average TX interface packets/s | Captures |
| --- | ---: | ---: | ---: | ---: | --- |
| 3 Gb/s | 2,999.495 Mb/s | 1.860% | 0.039 ms | 258,394 | [Summary](results/veth-100m-SZIjTQ/summary.txt) · [TX](results/veth-100m-SZIjTQ/tx.txt) · [RX](results/veth-100m-SZIjTQ/rx.txt) |

The reported bitrate was **99.98% of the target**. Linux counted 2,586,530 TX packets and 2,584,152 RX packets, with zero drops or errors in both interface logs. The TX rate uses the 10.010 s snapshot interval in the summary. iperf3's UDP loss measures gaps seen by the receiving application.

With a **0.5% UDP loss threshold** for comparison, this run exceeded the threshold. The best observed result below it remains **2.680 Gb/s with 0.166% loss** from the unpaced four-stream runs below. Repeated paced trials around 3 Gb/s with ten streams would establish where that configuration crosses the threshold.

### Unpaced packet rate

Two four-stream trials and two ten-stream trials used `-b 0` over the same veth pair, with a configured duration of 10 seconds. The zero bitrate disables iperf3's internal pacing limit. iperf3 selected the UDP payload size automatically. [iperf3 options](https://software.es.net/iperf/invoking.html)

| Streams / trial | TX interface packet delta | Average TX interface packets/s | iperf3 reported aggregate UDP bitrate | UDP loss | Captures |
| --- | ---: | ---: | ---: | ---: | --- |
| 4 / 1 | 2,226,297 | 222,629 | 2.581 Gb/s | 0.315% | [Summary](results/veth-100m-NsBFWk/summary.txt) · [TX](results/veth-100m-NsBFWk/tx.txt) · [RX](results/veth-100m-NsBFWk/rx.txt) |
| 4 / 2 | 2,310,177 | 231,018 | 2.680 Gb/s | 0.166% | [Summary](results/veth-100m-QMJEtJ/summary.txt) · [TX](results/veth-100m-QMJEtJ/tx.txt) · [RX](results/veth-100m-QMJEtJ/rx.txt) |
| 10 / 1 | 6,204,224 | 619,181 | 7.189 Gb/s | 34.793% | [Summary](results/veth-100m-ctrys6/summary.txt) · [TX](results/veth-100m-ctrys6/tx.txt) · [RX](results/veth-100m-ctrys6/rx.txt) |
| 10 / 2 | 6,373,006 | 636,625 | 7.387 Gb/s | 35.614% | [Summary](results/veth-100m-rdMDpe/summary.txt) · [TX](results/veth-100m-rdMDpe/tx.txt) · [RX](results/veth-100m-rdMDpe/rx.txt) |

The second ten-stream trial ran after the harness split and produced the highest observed average at **636,625 TX interface packets/s** over the 10.011 s TX snapshot interval. That is **2.76×** the best four-stream average. The rate divides the TX counter delta by the elapsed time between the two TX snapshots used in the summary. The interface tally includes any control traffic counted during that interval.

Both ten-stream trials recorded zero TX or RX interface drops or errors alongside **34.793–35.614% UDP loss**. The second trial counted **6,369,796 RX interface packets**. The interface counters describe packets at the veth devices, while iperf3 estimates UDP loss from sequence gaps at the receiving application. The captures leave the drop location unknown. The reported UDP bitrates come from iperf3's aggregate `end.sum`. Receiver goodput requires separate receiver output.

[Four-stream screenshot](../screenshots/veth-smoke-tests/01_baseline_max_throughput.png) · [First ten-stream trial screenshot](../screenshots/veth-smoke-tests/01_baseline_max_throughput_10_streams.png)

### Reproduce the paced and unpaced smoke tests

Install Python 3.12 with venv support, `iperf3`, and `iproute2` on a Linux host. The launcher uses `sudo` to create the veth pair and configure the RX namespace. From the repository root:

```bash
cd 01_baseline
python3.12 -m venv .venv312
.venv312/bin/python3 -m pip install psutil pyroute2
./run.sh
```

[`run.sh`](run.sh) uses fixed interface names and the `172.31.251.0/24` test subnet. It removes leftover interfaces with those names and recreates the named RX namespace, so reserve the names for this test and run one instance at a time. Python 3.12's Linux `fork` default lets workers inherit the current script's stop event. Python 3.14's `forkserver` default requires passing that event explicitly to the workers.

The checked-in launcher passes `--bitrate 300M --duration 10` to [`common_test_harness.py`](common_test_harness.py). The client command in [`packet_generation.py`](packet_generation.py) uses `-b 300M -P 10` for the paced 3 Gb/s trial. Set `--bitrate 0` in `run.sh` to repeat the unpaced ten-stream test. Use the [unpaced four-stream revision](https://github.com/vjhawar12/linux-network-performance/tree/e3a351b830c9458dd0b79cc26dac2dd56fcbe89d/01_baseline) to reproduce the `-P 4 -b 0` comparison. The [paced four-stream revision](https://github.com/vjhawar12/linux-network-performance/tree/6d46e3e556b41db366a336534cca06ccb80d06e7/01_baseline) used a final `-b 2.5G` for a 10 Gb/s aggregate target.

Use the [single-stream revision](https://github.com/vjhawar12/linux-network-performance/tree/8a3e6ba3cca71654b0b73b0365a4bbfa957239e9/01_baseline) for the original comparison. In that revision, `--bitrate 10G`, `1G`, or `100M` in `run.sh` sets the single-stream target.

The launcher creates a fresh `results/veth-100m-*/` directory for each run and writes `summary.txt`, `tx.txt`, and `rx.txt`. The published bitrate-targeted folders are named by aggregate UDP target and stream count. It prints a `tail -F` command to follow the logs in batches about once per second.

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

- [`run.sh`](run.sh) creates the veth pair, makes a run directory, and invokes the Python harness.
- [`common_test_harness.py`](common_test_harness.py) owns the CLI, namespace and interface configuration, traffic test timing, iperf3 client execution and JSON parsing, and reporting. It uses per-interface worker processes for Netlink counters and link events. Asyncio tasks drain their snapshots through pipes and append them to the TX/RX logs.
- [`packet_generation.py`](packet_generation.py) starts the iperf3 server inside the RX namespace and builds the iperf3 client command. The client sends UDP traffic, and the server receives it.

The runner lives in Stage 1. The [shared design](../common/README.md) describes how setup, telemetry, and reporting can be reused across backends.

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
sudo 01_baseline/.venv312/bin/python3 01_baseline/common_test_harness.py \
  enp23s0f0np0 enp23s0f1np1 \
  --tx-ip 172.31.250.1 --rx-ip 172.31.250.2 \
  --rx-namespace rxbench --bitrate 1G --duration 10
```

The current iperf3 client uses ten streams, so the example's `--bitrate 1G` requests 1 Gb/s per stream and 10 Gb/s in aggregate. The script assigns /24 addresses and can remove an existing namespace named by the CLI. Use a dedicated test namespace and interfaces.

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

- The bitrate-targeted and unpaced veth smoke tests have summaries and TX/RX logs. Repeat the trials and check startup, error handling, and shutdown across runs.
- A known switch path is checked physically, not assumed.
- iperf3 completes, and validated before/after counter samples plus reproducible test settings are saved.
- Errors and shutdown do not strand the child processes or damage unrelated network configuration.
- Repeatable tests provide a baseline for [custom raw Ethernet](../02_af_packet/README.md).

Once functional, freeze a known Stage 1 revision for future comparisons. More elaborate error recovery or memory/performance optimization can follow observed problems; basic safety and sample correctness cannot.
