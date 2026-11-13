// packet transmission with raw sockets to bypass the network stack but not the kernel yet

#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <netinet/in.h>
#include <net/if_arp.h>
#include <cstring>
#include <string.h>
#include <iostream>

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
    if (argc == 2) {
        const char* intf_name = argv[0];
        unsigned char* mac_addr;  
        std::strncpy((char*)mac_addr, argv[1], 8); 
        int idx = intf_idx(intf_name); 
        char custom_payload[256] = {0}; 
        size_t len = sizeof(custom_payload); 
        if (idx > 0) {
            const sockaddr_ll dest = {
                .sll_family = AF_PACKET,
                .sll_protocol = ETH_P_802_EX1,
                .sll_ifindex = idx,
                .sll_hatype = ARPHRD_ETHER,
                .sll_pkttype = PACKET_HOST, // sll_pkttype makes sense only for the receiving interface
                .sll_addr = *mac_addr,
                .sll_halen = ETH_ALEN
            }; 
            socklen_t socklen = sizeof(dest); 
            int ret = sendto(fd, custom_payload, len, MSG_CONFIRM, reinterpret_cast<const sockaddr*>(&dest), socklen); 
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
    return 1;
}   