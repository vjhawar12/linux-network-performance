# Shared Architecture — Python Control Plane and C++ Traffic Engines

**Status: planned.** Stage 1 currently keeps its code in [`01_baseline/`](../01_baseline/README.md). Extract shared code only after that baseline is functionally tested, and only where multiple stages actually reuse it.

## Responsibility split

| Task | Python | Native C++ backends (Stages 2–4) |
| --- | --- | --- |
| Select backend, interface(s), test settings, duration | Owns | Accept configuration |
| Start/stop, readiness, lifecycle, failure reporting | Orchestrates | Signals/returns status |
| Construct and pace every frame | No hot-path work | Owns |
| Batching, rings, queue/buffer lifetime | No hot-path work | Owns |
| Collect Linux or native statistics | Aggregates, correlates | Provide backend-specific counters |
| Reports and cross-stage comparisons | Owns | Return structured measurements |

The Python runner should **get smaller as native engines mature**. Stage 2 may begin with a C++ executable launched by `asyncio.create_subprocess_exec`. Future stages may use persistent services or native Python bindings (e.g., pybind11) for configuration/status if useful. Whichever integration is chosen, **Python does not process each individual test frame**.

## Planned code extraction (not yet present)

```text
common/
├── python/
│   ├── runner.py          # backend selection / test lifecycle
│   ├── interfaces.py      # safe interface/namespace control
│   ├── telemetry.py       # pluggable NIC/driver/switch collectors
│   └── reports.py         # normalized saved records
└── cpp/
    ├── packet_format.hpp # canonical versioned L2 frame format
    └── results.hpp       # native result schema
```

These names describe **proposed modules**, not existing files. Avoid premature abstraction: begin Stage 2 independently, then extract real shared needs.

## A stable experiment/backend contract

Every backend should expose:

- **Input:** run ID, interface/port and direction, destination/source MAC for raw L2, frame size, offered bitrate or pps, duration and configuration version.
- **Lifecycle:** initialized, ready, running, stopped/finished, and actionable errors/timeouts.
- **Output:** sent/received frames, sequence missing/duplicate/out-of-order counts, timestamps, achieved throughput and packet rate, CPU and queue metrics where available.
- **Provenance:** identify whether a counter comes from the app, Linux rtnetlink, driver, DPDK, or hardware/switch.

Structured JSON can work initially for control and final results, even if the engine itself is a separate process. High-rate frame data should stay entirely in C++.

Keep the same Layer 2 test payload across Stages 2–4, and comparable Ethernet frame lengths and offered loads. Stage 1 UDP/iperf3 is a reference but differs in protocol overhead and loss-accounting semantics.
