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

class MacAddress {
private:
std::array<uint8_t, 6> mac_addr = {};
public:
    MacAddress(const std::string& mac_addr_str) {
        if  (mac_addr_str.size() != 17) {
            throw std::runtime_error("Invalid mac address passed!");
        }
        int parsed = sscanf(
            mac_addr_str.c_str(), 
            "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",  
            &mac_addr[0], &mac_addr[1], &mac_addr[2],
            &mac_addr[3], &mac_addr[4], &mac_addr[5]
        ); 
        if (parsed != 6) {
            throw std::runtime_error("Invalid mac address passed!"); 
        }
    }

    uint8_t get(size_t idx) const {
        if (idx >= 6) {
            throw std::runtime_error("Invalid index " + std::to_string(idx)); 
        }
        return mac_addr[idx]; 
    }
}; 

class EthernetFrame {
private:
    ether_header header = {}; 
    std::array<char, sizeof(header) + 256> full_ethernet_frame = {}; 
    size_t total_len = 0; 
public:
    // build the entire ethernet frame with a header and payload
    const size_t payload_len = 0;
    EthernetFrame(const MacAddress& source_mac_address, const MacAddress& dest_mac_address, char* custom_payload, size_t len) : payload_len(len) {
        if (len > 256) {
            throw std::runtime_error("Payload size must be <= 256"); 
        }
        total_len = payload_len + sizeof(header); 
        header.ether_dhost[0] = dest_mac_address.get(0);
        header.ether_dhost[1] = dest_mac_address.get(1);
        header.ether_dhost[2] = dest_mac_address.get(2);
        header.ether_dhost[3] = dest_mac_address.get(3);
        header.ether_dhost[4] = dest_mac_address.get(4);
        header.ether_dhost[5] = dest_mac_address.get(5);
        header.ether_shost[0] = source_mac_address.get(0);
        header.ether_shost[1] = source_mac_address.get(1);
        header.ether_shost[2] = source_mac_address.get(2);
        header.ether_shost[3] = source_mac_address.get(3);
        header.ether_shost[4] = source_mac_address.get(4);
        header.ether_shost[5] = source_mac_address.get(5);
        header.ether_type = htons(ETH_P_802_EX1); 
        std::memcpy(full_ethernet_frame.data(), &header, sizeof(header)); 
        std::memcpy(full_ethernet_frame.data() + sizeof(header), custom_payload, len); 
    }

    const char* get() const {
        return full_ethernet_frame.data(); 
    }

    size_t length() const {
        return total_len;
    }
}; 

class PacketSocket {
private:
    int fd = -1;
    sockaddr_ll dest = {};
    socklen_t socklen = sizeof(dest); 
public:
    PacketSocket(const MacAddress& dest_mac_address, int intf_idx) {
        fd = socket(AF_PACKET, SOCK_RAW, 0);
        if (fd == -1) {
            throw std::runtime_error("could not open socket! errno = " + std::to_string(errno)); 
        }
        dest.sll_family = AF_PACKET;
        dest.sll_protocol = htons(ETH_P_802_EX1);
        dest.sll_ifindex = intf_idx;
        dest.sll_hatype = 0;
        dest.sll_pkttype = 0; // sll_pkttype makes sense only for the receiving interface
        dest.sll_addr[0] =  dest_mac_address.get(0);
        dest.sll_addr[1] =  dest_mac_address.get(1);
        dest.sll_addr[2] =  dest_mac_address.get(2);
        dest.sll_addr[3] =  dest_mac_address.get(3);
        dest.sll_addr[4] =  dest_mac_address.get(4);
        dest.sll_addr[5] =  dest_mac_address.get(5);
        dest.sll_halen = ETH_ALEN; 
    }
    // block copy constructor / assignment operator because that lets double-close on the same fd be possible
    PacketSocket(const PacketSocket& other) = delete;
    PacketSocket& operator= (const PacketSocket& other) = delete;
    // the move constructor / assignment operator is supported 
    // however the default move constructor / assignment operator won't invalidate the old fd
    // which could let a double-close on the same fd happen
    PacketSocket(PacketSocket&& other) noexcept {
        if (this != &other) {
            if (other.fd >= 0) {
                close(other.fd); 
            }
            this->fd = other.fd;
            other.fd = -1;
            std::memcpy(&this->dest, &other.dest, sizeof(other.dest));
            memset(&other.dest, 0, sizeof(other.dest)); 
            this->socklen = other.socklen;
            other.socklen = 0; 
        }
    }

    PacketSocket& operator= (PacketSocket&& other) noexcept {
        if (this != &other) {
            if (other.fd >= 0) {
                close(other.fd); 
            }
            this->fd = other.fd;
            other.fd = -1;
            std::memcpy(&this->dest, &other.dest, sizeof(other.dest));
            memset(&other.dest, 0, sizeof(other.dest)); 
            this->socklen = other.socklen;
            other.socklen = 0; 
        }
        return *this;
    }

    int get_fd() const {
        return fd;
    }

    socklen_t get_socklen() const {
        return socklen;
    }

    const sockaddr_ll* get_sockaddr() const {
        return &dest;
    }

    ~PacketSocket() {
        if (fd >= 0) {
            close(fd); 
        }
    }
}; 

int intf_idx(const char* intf_name) {
    int idx = if_nametoindex(intf_name);
    if (idx == 0) {
        std::cerr << "Could not get interface index for " << intf_name << std::endl; 
        return -1; 
    }
    return idx; 
}

int main(int argc, char *argv[]) {
    if (argc == 5) {
        const char* source_intf_name = argv[1];
        int idx = intf_idx(source_intf_name); 
        try {
            MacAddress dest_mac_address(argv[2]); 
            MacAddress source_mac_address(argv[3]); 
            // opening raw socket
            PacketSocket raw_socket(dest_mac_address, idx); 
            char custom_payload[256]{};
            if (strlen(argv[4]) <= sizeof(custom_payload)) {
                memcpy(custom_payload, argv[4], strlen(argv[4])); 
            } else {
                std::cerr << "Payload max size " << sizeof(custom_payload) <<  " bytes!" << std::endl;
                return 1;
            }
            EthernetFrame frame(source_mac_address, dest_mac_address, custom_payload, strlen(custom_payload));
            int ret = sendto(
                raw_socket.get_fd(), 
                frame.get(), 
                frame.length(), 
                0, 
                reinterpret_cast<const sockaddr*>(raw_socket.get_sockaddr()), 
                raw_socket.get_socklen()
            ); 
            if (ret == -1) {
                std::cerr << "sendto failed with errno = " << std::to_string(errno) << std::endl;
                return 1;
            }
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl; 
            return 1;
        }
    } else {
        return 1;
    }
    return 0;
}   