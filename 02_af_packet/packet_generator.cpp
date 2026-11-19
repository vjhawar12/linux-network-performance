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
#include <string.h>
#include <cstring>
#include "helpers.h"
#include "packet_generator.h"

template <uint32_t packet_size>
PacketGenerator<packet_size>::PacketGenerator(
    MacAddress source_mac_address, 
    MacAddress dest_mac_address, 
    int intf_idx, 
    std::array<uint8_t, packet_size - 14>& custom_payload,
    uint64_t num_packets
) : frame(
    source_mac_address, 
    dest_mac_address, 
    custom_payload.data(), 
    custom_payload.size()
), raw_socket(
    dest_mac_address, intf_idx
) {
    this->num_packets = num_packets;  
}

template <uint32_t packet_size>
void PacketGenerator<packet_size>::generate() {
    int ret = 0; 
    for (uint64_t i = 0; i < num_packets; i++) {
        frame.set_sequence_number(i); 
        ret = sendto(
            raw_socket.get_fd(), 
            frame.get(),    
            frame.length(), 
            0, 
            reinterpret_cast<const sockaddr*>(raw_socket.get_sockaddr()), 
            raw_socket.get_socklen()
        ); 
        if (ret == -1) {
            throw std::runtime_error("sendto failed with errno = " + std::to_string(errno)); 
        } else {
            transmitted_packets++;
        }
    }
}

template <uint32_t packet_size>
unsigned long PacketGenerator<packet_size>::get_tx_received() {
    return transmitted_packets;
}

template class PacketGenerator<256>;