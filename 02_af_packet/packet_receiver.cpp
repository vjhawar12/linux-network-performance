#include "packet_receiver.h"
#include <poll.h>
#include <cstdint>
#include <utility>
#include <iostream>

template <uint32_t packet_size>
PacketReceiver<packet_size>::PacketReceiver(
    MacAddress dest_mac_address, 
    int intf_idx,
    timeval tv,
    uint64_t num_packets_expected
) : 
    raw_socket(
    dest_mac_address,
    intf_idx, 
    SOL_SOCKET,
    SO_RCVTIMEO,
    &tv,
    sizeof(timeval)
), num_packets_expected(num_packets_expected) {
    buf = new char[num_packets_expected * packet_size];  
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
    poll(&_pollfd, 1, deadline_seconds * 1000 / num_packets_expected); 
    auto start = std::chrono::steady_clock::now();
    while (num_packets_received < num_packets_expected && std::chrono::steady_clock::now() - start < deadline_seconds) {
        if (_pollfd.revents & POLLIN) {
            bytes_recvd = recv(raw_socket.get_fd(), buf + num_packets_received, packet_size, 0); 
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

template class PacketReceiver<256>;