# Stage 3 — C++ AF_XDP

**Status: planned.** This experiment follows the [AF_PACKET Layer 2 test](../02_af_packet/README.md). It investigates how shared buffers, descriptor rings, and batching affect the cost of moving the same raw Ethernet traffic between a C++ application and the NIC. [Stage 1](../01_baseline/README.md) remains the UDP/IP benchmark for throughput and loss.

[Main roadmap](../README.md) · [Stage 2](../02_af_packet/README.md) · [Shared design](../common/README.md)

## Engineering question

Can AF_XDP improve throughput, packets/s, or CPU efficiency compared with AF_PACKET by using UMEM, ring descriptors, batching, and **zero-copy when supported**?

AF_XDP uses Linux XDP and driver facilities. Verify the kernel, NIC, driver, and queue capabilities, and record whether each run uses copy or zero-copy mode.

## Intended implementation

```text
Python runner / configuration
          |
C++ AF_XDP sender + receiver
          |
UMEM + fill/completion/RX/TX descriptor rings
          |
NIC driver / XDP / queues -> switch -> NIC
          |
Native counters and results -> Python reports
```

1. Validate actual NIC, kernel and driver AF_XDP capabilities (both TX and RX).
2. Reuse Stage 2's **same custom Ethernet frame format** and sequence semantics.
3. Implement and test UMEM and ring setup, queue binding, buffer ownership, recycling and safe teardown.
4. Add burst processing, batching, buffer reuse and pacing, with measured packet-rate/CPU improvements.
5. Compare copy and zero-copy modes where available, and compare AF_XDP against AF_PACKET under the same traffic profile.
6. Explore queues/CPU/NUMA after a correct single-queue implementation.

Python should control the C++ engine at experiment-level granularity, never per frame. Native bindings or a subprocess control channel can be selected based on actual integration needs.

## Measurements and target

Track offered and achieved frames/s, payload throughput, Ethernet line rate, sequence gaps, NIC counters, and CPU use. Start with a single queue and repeatable comparisons on supported hardware. A 10–40 GbE test range is a hardware goal for the experiment.
