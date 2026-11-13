/* 
Using a traditional network socket with AF_INET and a TCP/UDP payload would force the packet to follow this path roughly: 
user process -> ip stack (routing tables, firewall rules, etc) -> kernel -> switch -> NIC on the TX side -> NIC on the RX side -> system DRAM
This doesn't test the maximum throughput of packet flow through the switch as well because the packets aren't going directly to the switch

Because we're just testing layer 2 functionality, we don't need to involve the IP stack at all and can just work with MAC addresses. This means we 
don't need a traditional network socket with AF_INET and can instead use a raw socket with a custom payload--not a UDP/TCP payload. This would let the 
packet path be more like this: user process -> kernel -> switch -> NIC on TX side -> NIC on RX side -> system DRAM
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

int main(int argc, char *argv[]) {
    // opening raw socket
    int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd == -1) {
        // check errno
        return 1;
    } 
    if (argc == 4) {
        const char* intf_name = argv[1];
        std::array<uint8_t, 6> dest_mac_address = {}; 
        for (int i = 0; i < 6; i++) {
            dest_mac_address[i] = argv[2][i]; 
        }
        int idx = intf_idx(intf_name); 
        char custom_payload[256] = {0}; 
        size_t len = sizeof(custom_payload); 
        std::strncpy(custom_payload, argv[3], len); 
        ether_header header = {
            .ether_dhost = {
                dest_mac_address[0],
                dest_mac_address[1],
                dest_mac_address[2],
                dest_mac_address[3],
                dest_mac_address[4],
                dest_mac_address[5],
            },
            .ether_shost = {
                dest_mac_address[0],
                dest_mac_address[1],
                dest_mac_address[2],
                dest_mac_address[3],
                dest_mac_address[4],
                dest_mac_address[5],
            },
            .ether_type = ETH_P_802_EX1
        };  
        if (idx > 0) {
            const sockaddr_ll dest = {
                .sll_family = AF_PACKET,
                .sll_protocol = ETH_P_802_EX1,
                .sll_ifindex = idx,
                .sll_hatype = ARPHRD_ETHER,
                .sll_pkttype = PACKET_HOST, // sll_pkttype makes sense only for the receiving interface
                .sll_addr = {
                    dest_mac_address[0],
                    dest_mac_address[1],
                    dest_mac_address[2],
                    dest_mac_address[3],
                    dest_mac_address[4],
                    dest_mac_address[5],
                },
                .sll_halen = ETH_ALEN
            }; 
            socklen_t socklen = sizeof(dest); 
            char full_buffer[256];
            std::memcpy(full_buffer, &header, sizeof(header)); 
            std::memcpy(full_buffer + sizeof(header), custom_payload, sizeof(custom_payload)); 
            len = sizeof(full_buffer); 
            int ret = sendto(fd, full_buffer, len, MSG_CONFIRM, reinterpret_cast<const sockaddr*>(&dest), socklen); 
            close(fd); 
            if (ret == -1) {
                // check errno
                return 1;
            }
        } else {
            return 1;
        }
    } else {
        return 1;
    }
    return 0;
}   