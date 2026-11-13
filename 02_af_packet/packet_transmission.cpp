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


#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <netinet/in.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <string.h>
#include <cstring>
#include <iostream>
#include <array>

int intf_idx(const char* intf_name) {
    int idx = if_nametoindex(intf_name);
    if (idx == 0) {
        std::cerr << "Could not get interface index for " << intf_name << std::endl; 
        return -1; 
    }
    return idx; 
}

int mac_address_parsing(const std::string& mac_addr_str, std::array<uint8_t, 6>& mac_addr) {
    int parsed = sscanf(
        mac_addr_str.c_str(), 
        "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",  
        &mac_addr[0], &mac_addr[1], &mac_addr[2],
        &mac_addr[3], &mac_addr[4], &mac_addr[5]
    ); 
    if (parsed == 6) {
        return 0;
    }
    return 1; 
}

int main(int argc, char *argv[]) {
    // opening raw socket
    int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd == -1) {
        // check errno
        return 1;
    } 
    if (argc == 5) {
        const char* source_intf_name = argv[1];
        std::array<uint8_t, 6> dest_mac_address = {}; 
        std::array<uint8_t, 6> source_mac_address = {}; 
        if (mac_address_parsing(argv[2], dest_mac_address) == 0) {
            if (mac_address_parsing(argv[3], source_mac_address) == 0) {
                int idx = intf_idx(source_intf_name); 
                char custom_payload[256] = {0}; 
                size_t len = sizeof(custom_payload); 
                std::strncpy(custom_payload, argv[4], len); 
                ether_header header = {};  
                header.ether_dhost[0] = dest_mac_address[0];
                header.ether_dhost[1] = dest_mac_address[1];
                header.ether_dhost[2] = dest_mac_address[2];
                header.ether_dhost[3] = dest_mac_address[3];
                header.ether_dhost[4] = dest_mac_address[4];
                header.ether_dhost[5] = dest_mac_address[5];
                header.ether_shost[0] = source_mac_address[0];
                header.ether_shost[1] = source_mac_address[1];
                header.ether_shost[2] = source_mac_address[2];
                header.ether_shost[3] = source_mac_address[3];
                header.ether_shost[4] = source_mac_address[4];
                header.ether_shost[5] = source_mac_address[5];
                if (idx > 0) {
                    sockaddr_ll dest = {};
                    dest.sll_family = AF_PACKET,
                    dest.sll_protocol = htons(ETH_P_802_EX1),
                    dest.sll_ifindex = idx,
                    dest.sll_hatype = ARPHRD_ETHER,
                    dest.sll_pkttype = PACKET_HOST, // sll_pkttype makes sense only for the receiving interface
                    dest.sll_addr[0] =  dest_mac_address[0];
                    dest.sll_addr[1] =  dest_mac_address[1];
                    dest.sll_addr[2] =  dest_mac_address[2];
                    dest.sll_addr[3] =  dest_mac_address[3];
                    dest.sll_addr[4] =  dest_mac_address[4];
                    dest.sll_addr[5] =  dest_mac_address[5];
                    dest.sll_halen = ETH_ALEN; 
                    socklen_t socklen = sizeof(dest); 
                    std::array<char, sizeof(header) + 256> full_buffer; 
                    std::memcpy(full_buffer.data(), &header, sizeof(header)); 
                    std::memcpy(full_buffer.data() + sizeof(header), custom_payload, sizeof(custom_payload)); 
                    len = sizeof(full_buffer); 
                    int ret = sendto(fd, full_buffer.data(), len, MSG_CONFIRM, reinterpret_cast<const sockaddr*>(&dest), socklen); 
                    close(fd); 
                    if (ret == -1) {
                        // check errno
                        return 1;
                    }
                } else {
                    return 1;
                }
            }
        } else {
            return 1;
        }
    } else {
        return 1;
    }
    return 0;
}   