#include "packet_receiver.h"
#include <cstdint>
#include <iostream>
#include <chrono>
#include <atomic>
#include <functional>

template <uint32_t packet_size>
PacketReceiver<packet_size>::PacketReceiver(
    MacAddress dest_mac_address, 
    int intf_idx,
    uint64_t num_packets_expected,
    std::chrono::steady_clock::time_point deadline
) : 
deadline(
    deadline
), raw_socket(
    dest_mac_address,
    intf_idx
), num_packets_expected(num_packets_expected) {
    buf = new char[packet_size];  
    try {
        raw_socket.bind_socket();
    } catch (std::exception& e) {
        throw PacketReceiverError("Unable to bind socket!");
    }
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000; // 20 ms
    if (setsockopt(raw_socket.get_fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        throw PacketReceiverError("Unable to configure socket timeout!");
    }
}

template<uint32_t packet_size>
void PacketReceiver<packet_size>::_receive(std::chrono::steady_clock::time_point _deadline, std::function<bool()>&& cond) {
    while (cond() && !ELAPSED_MS(std::chrono::steady_clock::now(), _deadline)) {
        ssize_t bytes_recvd = 0;
        // blocking recv with bounded timeout (default 20 ms) to avoid rechecking cond() infrequently
        bytes_recvd = recv(raw_socket.get_fd(), buf, packet_size, 0); 
        if (bytes_recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                timeouts_++;
            } else if (errno == EINTR) {
                continue;
            } else {
                throw PacketReceiverError("recv error: errno = " + std::to_string(errno)); 
            }
        } else {
            num_packets_received++;
        }
    }
}

template <uint32_t packet_size>
void PacketReceiver<packet_size>::receive(std::atomic<bool>& generator_finished) {
    _receive(deadline, [&generator_finished](){return !generator_finished.load();});
    // drain queue for a short period after generator finishes to catch stragglers
    auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    _receive(drain_deadline, [&drain_deadline](){return std::chrono::steady_clock::now() < drain_deadline;});
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