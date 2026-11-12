# Stage 3 — C++ AF_XDP

**Status: planned.** Keep Python as the orchestration/reporting layer and implement an AF_XDP packet engine in C++ to explore high packet rates with shared buffers and descriptor rings.

[Main roadmap](../README.md) · [Stage 2](../02_af_packet/README.md) · [Shared design](../common/README.md)

## Engineering question

Can AF_XDP improve throughput, packets/s, or CPU efficiency compared with AF_PACKET by using UMEM, ring descriptors, batching, and **zero-copy when supported**?

AF_XDP uses Linux XDP/driver facilities; it is not a universal promise of complete kernel bypass or zero copy. Driver, kernel, NIC and queue support must be verified and copy vs zero-copy mode recorded.

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

Track offered and achieved frames/s, application payload vs Ethernet line rate, sender/receiver sequence gaps, NIC counters, and CPU. Initial ambition is 10–40 GbE; **AF_XDP has no intrinsic 40G ceiling**. A feature-complete stage requires reproducible measurements on supported hardware, not merely compiling an AF_XDP sample.
