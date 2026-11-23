#include "packet_receiver.h"
#include <poll.h>
#include <cstdint>
#include <utility>
#include <iostream>
#include <chrono>
#include <atomic>
#include <functional>

template <uint32_t packet_size>
PacketReceiver<packet_size>::PacketReceiver(
    MacAddress dest_mac_address, 
    int intf_idx,
    uint64_t num_packets_expected,
    uint16_t duration_s
) : raw_socket(
    dest_mac_address,
    intf_idx
), duration_s(
    duration_s
), num_packets_expected(num_packets_expected) {
    buf = new char[packet_size];  
    try {
        raw_socket.bind_socket();
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000; // 20 ms
        setsockopt(raw_socket.get_fd(), SO_RCVTIMEO, SO_SNDTIMEO, &tv, sizeof(tv)); 
    } catch (std::exception& e) {
        throw PacketReceiverError("Unable to bind socket!");
    }
}

template<uint32_t packet_size>
void PacketReceiver<packet_size>::_receive(pollfd& _pollfd, std::chrono::steady_clock::time_point _deadline, std::function<bool()>&& cond) {
    while (cond()) {
        // using poll prevents blocking forever with recv and never exiting the loop
        ssize_t bytes_recvd = 0;
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(_deadline - std::chrono::steady_clock::now()).count();
        if (diff <= 0) {
            break;
        }
        // blocking recv with bounded timeout (default 20 ms) to avoid rechecking cond() infrequently
        diff = static_cast<int>(std::min<decltype(diff)>(diff, timeout_ms)); 
        bytes_recvd = recv(raw_socket.get_fd(), buf, packet_size, 0); 
        if (bytes_recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                __timeouts++;
            } else {
                std::cerr << "recv error: errno = " << errno;
            }
        } else {
            num_packets_received++;
        }
    }
}

template <uint32_t packet_size>
void PacketReceiver<packet_size>::receive(std::atomic<bool>& generator_finished) {
    pollfd _pollfd{};
    _pollfd.fd = raw_socket.get_fd();
    _pollfd.events = POLLIN;
    auto start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point _deadline = start + std::chrono::seconds(duration_s);
    _receive(_pollfd, _deadline, [&generator_finished](){return !generator_finished.load();});
    // drain queue for a short period after generator finishes to catch stragglers
    auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    _receive(_pollfd, drain_deadline, [&drain_deadline](){return std::chrono::steady_clock::now() < drain_deadline;});
    // retrieve socket statistics from PACKET_STATISTICS structure
    // it gets reset upon reading, so we read it once at the end
    struct tpacket_stats stats;
	socklen_t len = sizeof(stats);
    if (getsockopt(raw_socket.get_fd(), SOL_PACKET, PACKET_STATISTICS, &stats, &len)) {
        throw PacketReceiverError("Can't retrieve PACKET_STATISTICS: errno = " + std::to_string(errno));
    } else {
        tp_drops = stats.tp_drops;
        tp_packets = stats.tp_packets;
    }
}

template <uint32_t packet_size>
std::tuple<uint64_t, uint64_t, uint32_t, uint32_t> PacketReceiver<packet_size>::get() {
    return std::make_tuple(num_packets_expected, num_packets_received, tp_drops, tp_packets); 
}

template <uint32_t packet_size>
PacketReceiver<packet_size>::~PacketReceiver() {
    delete[] buf;
}

template class PacketReceiver<PACKET_SIZE_BYTES>;