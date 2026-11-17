# Stage 2 — C++ AF_PACKET Raw Ethernet

**Status (16 November 2026).** Stage 2 builds as a C++17 executable and includes a raw Ethernet sender and a native receiver prototype. An earlier sender revision delivered a single Ethernet frame across a WSL2 `veth` pair, verified with `tcpdump`. Repeated transmission from the current build and delivery to the native receiver await validation.

[Main roadmap](../README.md) · [Stage 1](../01_baseline/README.md) · [Shared design](../common/README.md)

## Engineering question

How do frame rate, CPU utilization, and loss accounting compare with the Stage 1 UDP/IP baseline when the benchmark constructs and sends its own Layer 2 frames? `AF_PACKET` uses the Linux packet-socket and driver path. [Linux packet sockets](https://man7.org/linux/man-pages/man7/packet.7.html)

## Current implementation

- [`packet_generator.cpp`](packet_generator.cpp) reuses one prebuilt Ethernet frame with experimental EtherType `0x88B5` and calls `sendto()` for a configurable frame count. The CLI accepts text payloads up to 256 bytes. Each frame in a run has the same payload.
- [`packet_receiver.cpp`](packet_receiver.cpp) calls `recv()` and increments its count on successful calls. It stores the signed return value as `ssize_t` so negative returns enter the error path. [`app.cpp`](app.cpp) calls the generator first, then the receiver, and prints expected and received counts if both calls return.
- [`helpers.cpp`](helpers.cpp) handles MAC parsing, frame construction, and socket ownership. It also has an experimental rtnetlink interface dump. The executable resolves the TX interface with `if_nametoindex()`.

### Native receive limitations

The CLI passes the TX interface index to both objects, and the receiver opens its packet socket with protocol 0 and never binds it. Linux requires a nonzero protocol for packet delivery. Bind the receiver to the peer interface and EtherType `0x88B5` before starting TX. [Linux packet sockets](https://man7.org/linux/man-pages/man7/packet.7.html)

The timeout currently sets only `timeval.tv_usec`, and the socket setup ignores the result of `setsockopt()`. The receive loop retries every error and only exits when the expected count is reached. Initialize the timeout, check socket setup, and give each run a deadline so it can report a short receive count. Drain RX while TX is active for sustained tests.

## Build and peer capture

Build from the repository root with CMake 3.23 or later and a C++17 compiler on Linux.

```bash
cmake -S . -B build
cmake --build build
```

Create a temporary virtual Ethernet pair on a test host. The commands require privileges to configure interfaces and open packet sockets.

```bash
sudo ip link add veth-tx type veth peer name veth-rx
sudo ip link set veth-tx up
sudo ip link set veth-rx up
```

In one terminal, capture ten frames from the RX peer.

```bash
sudo tcpdump -i veth-rx -n -e -XX -c 10 'ether proto 0x88b5'
```

In another terminal, run this command with the TX interface, source MAC, destination MAC, text payload, and frame count in that order. The outer five-second timeout bounds the current executable, which can hang after sending while it waits for native RX.

```bash
sudo timeout 5s ./build/linux_network_performance veth-tx \
    "$(cat /sys/class/net/veth-tx/address)" \
    "$(cat /sys/class/net/veth-rx/address)" \
    "Hello" 10
```

Use the peer capture to check frame count, MAC addresses, EtherType, and payload. This procedure checks delivery independently of the current native receiver. After the test, remove the temporary pair with `sudo ip link del veth-tx`.

## Validation status

| Capability | Status |
| --- | --- |
| One raw Ethernet frame across a WSL2 veth pair | Verified with `tcpdump` in an earlier sender revision |
| Configurable transmit loop | Implemented, current revision awaits peer-side capture |
| Native receive loop and expected/received counts | Prototype, peer socket setup and bounded completion pending |
| Sequence gaps, duplicates, and out-of-order delivery | Planned |
| Timed throughput/loss comparison and physical switch forwarding | Planned |

After the ten-frame capture passes, verify that a native RX run reports both full and short receive counts within a deadline. Add a run ID and sequence number before using the counts to measure frame loss, then record elapsed time and frame size for comparison with Stage 1. A successful `sendto()` counts kernel submission. Use receiver, interface, and switch measurements to investigate where frames go missing.
