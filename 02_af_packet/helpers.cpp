#include <stdint.h>
#include <stddef.h>
#include <array>
#include <string>
#include <stdexcept>
#include <cstring>
#include <netinet/if_ether.h>
#include <netinet/in.h> 
#include <linux/if_packet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <netinet/in.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <string.h>
#include <cstring>
#include <iostream>
#include <array>
#include <vector>
#include "helpers.h" 
#include <arpa/inet.h>

MacAddress::MacAddress(const std::string& mac_addr_str) {
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

uint8_t MacAddress::get(size_t idx) const {
    if (idx >= 6) {
        throw std::runtime_error("Invalid index " + std::to_string(idx)); 
    }
    return mac_addr[idx]; 
}


template<uint16_t MAX_PAYLOAD_LENGTH>
EthernetFrame<MAX_PAYLOAD_LENGTH>::EthernetFrame(const MacAddress& source_mac_address, const MacAddress& dest_mac_address, uint8_t* custom_payload, size_t len) : payload_len(len) {
    if (len > MAX_PAYLOAD_LENGTH) {
        throw std::runtime_error("Payload size must be <= MAX_PAYLOAD_LENGTH"); 
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

template<uint16_t MAX_PAYLOAD_LENGTH>
void EthernetFrame<MAX_PAYLOAD_LENGTH>::set_sequence_number(uint64_t _num) {
    uint8_t* ptr = (uint8_t*)full_ethernet_frame.data() + ETHERNET_HEADER_BYTES + RUN_ID_BYTES;
    uint64_t num = htobe64(_num); //  enforcing big endian order of bytes for sequence num; ensure rx side agrees
    std::memcpy(ptr, &num, sizeof(num));  
}

template<uint16_t MAX_PAYLOAD_LENGTH>
const char* EthernetFrame<MAX_PAYLOAD_LENGTH>::get() const {
    return full_ethernet_frame.data(); 
}

template<uint16_t MAX_PAYLOAD_LENGTH>
size_t EthernetFrame<MAX_PAYLOAD_LENGTH>::length() const {
    return total_len;
}

PacketSocket::PacketSocket(const MacAddress& dest_mac_address, int intf_idx) {
    this->intf_idx = intf_idx;
    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_EX1));
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

PacketSocket::PacketSocket(
    const MacAddress& dest_mac_address, int intf_idx, 
    int sockopt, int level, void* optvalue, size_t optlen
) 
: PacketSocket(dest_mac_address, intf_idx)
{
    setsockopt(fd, sockopt, level, optvalue, optlen);  
}

PacketSocket::PacketSocket(PacketSocket&& other) noexcept {
    if (this != &other) {
        this->fd = other.fd;
        other.fd = -1;
        std::memcpy(&this->dest, &other.dest, sizeof(other.dest));
        memset(&other.dest, 0, sizeof(other.dest)); 
        this->socklen = other.socklen;
        other.socklen = 0; 
    }
}

PacketSocket& PacketSocket::operator= (PacketSocket&& other) noexcept {
    if (this != &other) {
        if (this->fd >= 0) {
            close(this->fd); 
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

void PacketSocket::bind_socket() {
    sockaddr_ll addr;
    addr.sll_family = AF_PACKET;
    addr.sll_ifindex = intf_idx; 
    addr.sll_pkttype = PACKET_HOST;
    addr.sll_hatype = ARPHRD_ETHER; 
    addr.sll_protocol = htons(ETH_P_802_EX1); 
    int ret = bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)); 
    if (ret != 0) {
        throw std::runtime_error("Unable to bind socket! Errno = " + std::to_string(errno)); 
    }
}

int PacketSocket::get_fd() const {
    return fd;
}

socklen_t PacketSocket::get_socklen() const {
    return socklen;
}

const sockaddr_ll* PacketSocket::get_sockaddr() const {
    return &dest;
}

PacketSocket::~PacketSocket() {
    if (fd >= 0) {
        close(fd); 
    }
}

int intf_name_to_index(std::string intf_name) {
    int idx = if_nametoindex(intf_name.c_str()); 
    if (idx <= 0) {
        throw std::runtime_error("Invalid interface index: " + std::to_string(idx));
    }
    return idx;
}

void intf_index_to_name(int index, char* buf, size_t len) {
    if (len < IF_NAMESIZE) {
        throw std::runtime_error("Buffer must be at least " + std::to_string(IF_NAMESIZE) + " bytes"); 
    }
    char* ifname = if_indextoname(index, buf); 
    if (ifname == NULL) {
        throw std::runtime_error("Invalid interface index: " + std::to_string(index));
    }
}

std::vector<NetworkInterface> discover() {
    std::vector<NetworkInterface> intf_list;
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    static uint32_t seq = 0;
    nlmsghdr nlhdr = {}; 
    nlhdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlhdr.nlmsg_type = RTM_GETLINK;
    nlhdr.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
    nlhdr.nlmsg_seq = seq++;
    ifinfomsg msg = {};
    msg.ifi_family = AF_PACKET;
    char req[sizeof(nlhdr) + sizeof(msg)]; 
    memcpy(req, &nlhdr, sizeof(nlhdr)); 
    memcpy(req + sizeof(nlhdr), &msg, sizeof(msg)); 
    if (send(fd, &req, nlhdr.nlmsg_len, 0) < 0) {
        close(fd);
        throw std::runtime_error("netlink send request failed with errno = " + std::to_string(errno)); 
    }
    bool done = false;
    alignas(nlmsghdr) char buf[8192]; 
    while (!done) {
        int len = recv(fd, buf, sizeof(buf), 0);
        nlmsgerr* err;
        if (len > 0) {
            for (nlmsghdr* nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (unsigned int)len); nh = NLMSG_NEXT(nh, len)) {
                switch (nh->nlmsg_type) {
                case NLMSG_ERROR:
                    err = (struct nlmsgerr *)NLMSG_DATA(nh);
                    if (err->error) {
                        close(fd);
                        throw std::runtime_error("error during message parsing with errno = " + std::to_string(errno)); 
                    }
                case NLMSG_DONE:
                    done = true;
                    break;
                case RTM_NEWLINK:
                    // parse link related information
                    int rem = IFLA_PAYLOAD(nh);
                    ifinfomsg* _ifinfo = static_cast<ifinfomsg*>(NLMSG_DATA(nh)); 
                    rtattr* attr = IFLA_RTA(_ifinfo);  
                    uint8_t* mac; 
                    NetworkInterface intf;
                    intf.intf_idx = _ifinfo->ifi_index;  
                    intf.link_up = _ifinfo->ifi_flags & IFF_LOWER_UP;
                    for (rtattr* attr = static_cast<rtattr*>(IFLA_RTA(_ifinfo)); RTA_OK(attr, rem); attr = RTA_NEXT(attr, rem)) {
                        switch (attr->rta_type) {
                            case IFLA_IFNAME:
                                intf.intf_name  = (char*)RTA_DATA(attr); 
                                break;
                            case IFLA_ADDRESS:
                                mac = (uint8_t*)RTA_DATA(attr); 
                                for (int i = 0; i < 6; i++) {
                                    intf.mac[i] = mac[i];
                                }
                                break;
                            case IFLA_MTU:
                                intf.mtu = *(uint32_t*)RTA_DATA(attr); 
                                break;
                        }
                    }
                    intf_list.push_back(intf); 
                    break;
                }
            }
        } else {
            close(fd);
            throw std::runtime_error("Error receving messages with errno = " + std::to_string(errno)); 
        }
    }
    close(fd); 
    return intf_list; 
}

void print(std::vector<NetworkInterface> intf_list) {
    if (intf_list.size() == 0) {
        std::cout << "No interfaces found" << std::endl;
    } else {
        std::cout << "Listing interfaces..." << std::endl;
        for (auto it = intf_list.begin(); it != intf_list.end(); it++) {
            std::cout << "Intf name: " <<  (*it).intf_name 
            << " idx: " << (*it).intf_idx 
            << " link " << (*it).link_up
            << std::endl;
        }
    }
}

template class EthernetFrame<256>;