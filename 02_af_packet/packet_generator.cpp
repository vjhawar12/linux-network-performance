/* 
Using a traditional network socket with AF_INET and a TCP/UDP payload would force the packet to follow this path roughly: 
user process -> ip stack (routing tables, firewall rules, etc) -> kernel -> NIC on the TX side -> Switch -> NIC on the RX side -> system DRAM
This doesn't test the maximum throughput of packet flow through the switch as well because the packets aren't going directly to the switch

Because we're just testing layer 2 functionality, we don't need to involve the IP stack at all and can just work with MAC addresses. This means we 
don't need a traditional network socket with AF_INET and can instead use a raw socket with a custom payload--not a UDP/TCP payload. This would let the 
packet path be more like this: user process -> kernel -> NIC on TX side -> Switch -> NIC on RX side -> system DRAM
This path is more direct and would let us have greather throughput of packets through the switch and would give a more accurate number
as to what is the real upper limit of packet throughput that the switch can handle before its packet drop statistics cross a significant amount
*/

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <netinet/in.h>
#include <net/if_arp.h>
#include "helpers.h"
#include "packet_generator.h"
#include <chrono>
#include <atomic>

template <uint32_t packet_size>
PacketGenerator<packet_size>::PacketGenerator(
    MacAddress source_mac_address, 
    MacAddress dest_mac_address, 
    int intf_idx, 
    std::array<uint8_t, packet_size - 14>& custom_payload,
    uint64_t bitrate_s,
    std::chrono::steady_clock::time_point deadline,
    std::atomic<bool>& finished
) : deadline(
    deadline
), frame(
    source_mac_address, 
    dest_mac_address, 
    custom_payload.data(), 
    custom_payload.size()
), raw_socket(
    dest_mac_address, intf_idx
), finished(
   finished
) {
    this->bitrate_s = bitrate_s;  
}

template <uint32_t packet_size>
void PacketGenerator<packet_size>::generate(uint32_t duration_s) {
    int ret = 0; 
    uint64_t sequence_idx = 0; 
    uint64_t total_packets = bitrate_s * duration_s / (packet_size * 8);
    uint16_t interval_ms = 200;  
    uint64_t regular_packet_load = (interval_ms / (1000 * duration_s)) * total_packets; 
    uint64_t extended_packet_load  = 1.5 * regular_packet_load;
    uint64_t packets_scheduled = regular_packet_load;
    uint64_t packets_sent_this_interval = 0; 
    auto interval_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    while (!ELAPSED_MS(std::chrono::steady_clock::now(), deadline) && transmitted_packets < total_packets) {
        packets_sent_this_interval = 0; 
        interval_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms); 
        while (!ELAPSED_MS(std::chrono::steady_clock::now(), interval_deadline)) {
            frame.set_sequence_number(sequence_idx); 
            ret = sendto(
                raw_socket.get_fd(), 
                frame.get(),    
                frame.length(), 
                0, 
                reinterpret_cast<const sockaddr*>(raw_socket.get_sockaddr()), 
                raw_socket.get_socklen()
            ); 
            if (ret == -1) {
                finished = true;
                throw PacketGeneratorError("sendto failed with errno = " + std::to_string(errno)); 
            } else {
                transmitted_packets++;
                packets_sent_this_interval++; 
                sequence_idx++;
            }
        }
        auto time_left_ms = TIME_LEFT_MS(std::chrono::steady_clock::now(), interval_deadline);
        if (time_left_ms == 0) continue;
        else if (time_left_ms > 0) {
            // schedule the usual load
            packets_scheduled = std::min(regular_packet_load, total_packets - transmitted_packets);
            // busy wait until the next interval
            high_performance_busy_wait(time_left_ms);     
        } else {
            // schedule a slightly larger load than usual to cover up for the packets that weren't sent
            // but not large enough to cause a microburst leading to artifical packet drop 
            packets_scheduled = std::min(extended_packet_load, total_packets - transmitted_packets);
        }
    }
    finished = true;
}

template <uint32_t packet_size>
unsigned long PacketGenerator<packet_size>::get_tx_received() {
    return transmitted_packets;
}

template class PacketGenerator<PACKET_SIZE_BYTES>;