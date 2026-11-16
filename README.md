# Linux Network Performance

A staged Linux networking project for **characterizing Ethernet NICs, physical links, and external switch forwarding**, starting with a software baseline over Linux virtual Ethernet and progressing to two physical NIC ports connected through an external switch. The long-term goal is to identify host, NIC, switch, and physical-link bottlenecks under increasing packet rates, including environmental vibration testing.

**Architecture:** one Python experiment controller, with progressively more of the packet generation/receiving implemented in C++. Stage 1 uses iperf3 as the traffic engine; Stages 2–4 use custom C++ engines. The Python component should become smaller and focus on orchestration, telemetry aggregation, and reporting—not per-packet operations.

> **Status (16 November 2026).** Stage 1 completed [UDP smoke tests on a Linux virtual Ethernet pair](01_baseline/README.md#virtual-ethernet-smoke-tests) with aggregate targets of **100 Mb/s**, **1 Gb/s**, **3 Gb/s**, and **10 Gb/s**. The trials ran inside one Linux host. At a 10 Gb/s aggregate target, iperf3 reported **1.062 Gb/s with one stream** and **2.604 Gb/s with four streams**. The [unpaced ten-stream veth trials](01_baseline/README.md#unpaced-packet-rate) reached a highest observed average of **about 637,000 TX interface packets/s** over 10 seconds. iperf3 reported **7.387 Gb/s** aggregate UDP bitrate and **35.614%** UDP loss in that trial. In the [paced ten-stream trial](01_baseline/README.md#paced-ten-stream-trial), iperf3 reported **2.999 Gb/s** with **1.860%** UDP loss at a 3 Gb/s aggregate target. Testing with physical NICs and an external switch, starting at 10 GbE, is planned. Stage 2 has an experimental C++ AF_PACKET transmitter.

## Four-stage packet-I/O roadmap

| Stage | Directory | Traffic mechanism | Core engineering question | Target experiment |
| --- | --- | --- | --- | --- |
| **1. Baseline** | [`01_baseline/`](01_baseline/README.md) | Python + iperf3 using UDP/IP | Can we prove the physical forwarding path and obtain trustworthy measurements? | Virtual smoke tests at 100 Mb/s, 1 Gb/s, 3 Gb/s, and 10 Gb/s aggregate targets. The 10 Gb/s target was tested with one and four streams. Unpaced trials used four and ten streams. |
| **2. Raw Ethernet** | [`02_af_packet/`](02_af_packet/README.md) | C++ sender/receiver using `AF_PACKET` | What changes when we control L2 frames and packet batching? | Compare on 10 GbE |
| **3. AF_XDP** | [`03_af_xdp/`](03_af_xdp/README.md) | C++ AF_XDP rings and UMEM | Does ring-based I/O and supported zero-copy improve packet-rate/CPU efficiency? | Explore 10–40 GbE |
| **4. DPDK** | [`04_dpdk/`](04_dpdk/README.md) | C++ DPDK Ethernet engine | How do multi-queue, multicore, NUMA, and NIC capabilities scale? | Long-term 100–400 GbE |

**Speed ranges are ambitions, not technology limits or guaranteed results.** AF_PACKET can run on faster links, AF_XDP is not capped at 40G, and DPDK is useful below 100G. Performance depends on hardware, driver, kernel, frame sizes, host processing, and workload. Each stage must be measured against the previous one; lower-level APIs are not inherently faster or more accurate.

Supporting tools are **not extra stages**: [kernel pktgen](diagnostics/kernel_pktgen/README.md) provides an independent packet-rate reference, and [kernel tracing](diagnostics/kernel_tracing/README.md) helps explain bottlenecks.

## Architecture and responsibilities

```text
                   Python experiment runner
      experiment config / lifecycle / backend selection
            interface setup / reporting / logs
                            |
                   traffic engine boundary
          +-----------------+------------------+
          |                 |                  |
       iperf3          C++ AF_PACKET      C++ AF_XDP / DPDK
       Stage 1           Stage 2           Stages 3–4
          +-----------------+------------------+
                            |
               TX NIC -> switch -> RX NIC
                            |
             telemetry + packet accounting
     rtnetlink / ethtool / backend NIC stats / switch counters
                            |
                     saved results
```

- **Python owns the control plane:** select backends, validate test settings, start/stop processes, supervise readiness and errors, collect telemetry, and compare results across runs.
- **C++ owns the data path for Stages 2–4:** construct Ethernet frames, pace and batch TX, receive and sequence frames, manage rings/buffers/queues, and expose results/counters.
- **Integrate at coarse granularity:** C++ executables controlled by Python subprocesses are a straightforward first approach. pybind11 or another native interface may be introduced if justified, but don't call Python once per packet.
- **Share code incrementally.** Stage 1 has an iperf3 command module and a stage-specific runner. Once Stage 2 exercises its own sender and receiver, move the setup and telemetry code both stages use into [`common/`](common/README.md).
- **Use backend-aware telemetry:** Linux rtnetlink works for conventional interfaces but may not expose NICs controlled through some DPDK PMDs; later engines must expose appropriate native statistics.

## Repository layout

```text
linux-network-performance/
├── 01_baseline/             # Python + iperf3 baseline
│   ├── common_test_harness.py
│   ├── packet_generation.py
│   ├── run.sh
│   ├── results/             # Published veth runs
│   └── README.md
├── 02_af_packet/            # Experimental C++ raw Ethernet
│   ├── packet_generation.cpp
│   └── README.md
├── 03_af_xdp/               # Planned C++ AF_XDP
│   └── README.md
├── 04_dpdk/                 # Planned C++ DPDK
│   └── README.md
├── common/                  # Planned shared runner, reports, test protocol
│   └── README.md
├── diagnostics/
│   ├── kernel_pktgen/       # Supporting packet generator comparison
│   │   └── README.md
│   └── kernel_tracing/      # Supporting bottleneck analysis
│       └── README.md
├── screenshots/
│   └── veth-smoke-tests/   # Terminal captures from virtual Ethernet runs
└── README.md                 # Main architecture and stage roadmap
```

The Stage 1 runner handles setup, telemetry, and reporting in [`common_test_harness.py`](01_baseline/common_test_harness.py). Its [`packet_generation.py`](01_baseline/packet_generation.py) launches the iperf3 server and builds the client command. The [Stage 1 README](01_baseline/README.md) has the veth quick start and raw captures. The [Stage 2 README](02_af_packet/README.md) describes the current transmitter and its validation status. Stages 3–4 and `common/` document the planned designs.

## Planned physical switch test topology

```text
                   One Linux host
          default netns             RX netns
           test sender            test receiver
                |                      ^
              TX NIC                 RX NIC
                |                      ^
                v                      |
            external switch: ingress -> egress
```

The initial proposed host includes Intel X710 10 GbE SFP+ ports (`i40e`), with `enp23s0f0np0` and `enp23s0f1np1` as candidate TX and RX ports, plus Intel X722 interfaces. **Cabling and physical switch-port mapping remain unverified.** The sample addresses `172.31.250.1` and `172.31.250.2` are unapproved placeholders.

With Stage 1's UDP/IP traffic, the RX namespace prevents Linux from delivering packets internally when both endpoints are on one host. Raw L2 backends can explicitly select the outgoing physical interface and Ethernet destination MAC, so the IP routing workaround may no longer be needed. In all cases, verify that the test actually traverses the intended physical switch rather than assuming so from socket results.

## Measurement contract across stages

Record the same categories consistently so backends can be compared:

- **Workload:** backend/version, run ID, sender/receiver NICs, queue, switch, frame size, payload format, test duration, offered rate, achieved bitrate/frames per second, and direction.
- **Per-frame accounting:** attempted/successful sends and received frames; custom Stage 2–4 frames share a versioned test ID/sequence number, with duplicate/missing/out-of-order accounting.
- **Hardware and software:** CPU consumption, Linux driver/NIC queue counters, link state and errors (including CRC where available), and switch ingress/egress counters when exposed.
- **Time and environment:** actual collection timestamps and intervals, missed polling deadlines, driver/kernel/NIC details, and eventually shaker-table measurements.

A receiving application seeing missing packets does **not** prove the switch dropped them: the loss could be in the sender, kernel, NIC, physical link, switch, or receiver. Use independent observation points. A nominal 10 ms stats poll provides observation granularity, **not exact timing of sub-10 ms vibration events**. Count Ethernet wire overhead (headers, FCS, preamble and interframe gap) when comparing line rates; use equivalent frame lengths for fair backend comparisons.

## Current priorities

1. **Extend the Stage 1 software baseline.** Validate the counter boundaries, repeat the paced ten-stream trials near 3 Gb/s, and capture UDP socket counters to investigate loss.
2. **Test on physical 10 GbE:** confirm switch forwarding and baseline throughput/loss/NIC counters before claims about hardware behavior.
3. **Validate Stage 2.** Check repeated AF_PACKET transmission, then add receiver-side accounting for comparison with the iperf3 baseline.
4. **Profile before escalating:** advance to AF_XDP and DPDK where the NIC supports them and measurements justify the complexity. Use diagnostic tools as needed.

## Safety

Use approved isolated NICs, addresses and switch ports. Never move or unbind a management interface, remove a namespace you do not own, or send disruptive high-rate traffic on a shared network. This repository is experimental and has no certified performance or reliability claims.
