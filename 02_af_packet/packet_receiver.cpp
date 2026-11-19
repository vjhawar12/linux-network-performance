#include "packet_receiver.h"
#include <poll.h>
#include <cstdint>
#include <utility>
#include <iostream>
#include <chrono>

template <uint32_t packet_size>
PacketReceiver<packet_size>::PacketReceiver(
    MacAddress dest_mac_address, 
    int intf_idx,
    uint64_t num_packets_expected
) : 
    raw_socket(
    dest_mac_address,
    intf_idx
), num_packets_expected(num_packets_expected) {
    buf = new char[packet_size];  
    try {
        raw_socket.bind_socket();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

template <uint32_t packet_size>
void PacketReceiver<packet_size>::receive(uint16_t deadline_seconds) {
    ssize_t bytes_recvd = 0;
    pollfd _pollfd{};
    _pollfd.fd = raw_socket.get_fd();
    _pollfd.events = POLLIN;
    auto start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point _deadline = start + std::chrono::seconds(deadline_seconds);
    while (num_packets_received < num_packets_expected && std::chrono::steady_clock::now() < _deadline) {
        // using poll prevents blocking forever with recv and never exiting the loop
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(_deadline - std::chrono::steady_clock::now()).count();
        poll(&_pollfd, 1, diff); 
        if (_pollfd.revents & POLLIN) {
            bytes_recvd = recv(raw_socket.get_fd(), buf, packet_size, 0); 
            if (bytes_recvd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("Receive timed out.\n");
                } else {
                    perror("recv error");
                }
                continue;
            } 
            num_packets_received++;
        }
    }
}

template <uint32_t packet_size>
std::pair<uint64_t, uint64_t> PacketReceiver<packet_size>::get() {
    return std::make_pair(num_packets_expected, num_packets_received); 
}

template <uint32_t packet_size>
PacketReceiver<packet_size>::~PacketReceiver() {
    delete[] buf;
}

template class PacketReceiver<PACKET_SIZE_BYTES>;