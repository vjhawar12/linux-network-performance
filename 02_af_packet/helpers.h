#ifndef HELPERS_H
#define HELPERS_H

#include <stdint.h>
#include <stddef.h>
#include <array>
#include <string>
#include <cstring>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <vector>

class MacAddress {
private:
std::array<uint8_t, 6> mac_addr = {};
public:
    MacAddress(const std::string& mac_addr_str);
    uint8_t get(size_t idx) const;
}; 

template<uint16_t MAX_PAYLOAD_LENGTH>
class EthernetFrame {
private:
    ether_header header = {}; 
    std::array<char, sizeof(header) + MAX_PAYLOAD_LENGTH> full_ethernet_frame = {}; 
    size_t total_len = 0; 
public:
    // build the entire ethernet frame with a header and payload
    const size_t payload_len;
    EthernetFrame(const MacAddress& source_mac_address, const MacAddress& dest_mac_address, uint8_t* custom_payload, size_t len); 
    const char* get() const ; 
    size_t length() const; 
}; 


class PacketSocket {
private:
    int fd = -1;
    sockaddr_ll dest = {};
    socklen_t socklen = sizeof(dest); 
public:
    PacketSocket(const MacAddress& dest_mac_address, int intf_idx);
    // block copy constructor / assignment operator because that lets double-close on the same fd be possible
    PacketSocket(const PacketSocket& other) = delete;
    PacketSocket& operator= (const PacketSocket& other) = delete;
    // the move constructor / assignment operator is supported 
    // however the default move constructor / assignment operator won't invalidate the old fd
    // which could let a double-close on the same fd happen
    PacketSocket(PacketSocket&& other) noexcept;
    PacketSocket& operator= (PacketSocket&& other) noexcept;
    int get_fd() const;
    socklen_t get_socklen() const;
    const sockaddr_ll* get_sockaddr() const;
    ~PacketSocket();
}; 

struct NetworkInterface {
    std::string intf_name{};
    size_t intf_idx = 0; 
    std::array<uint8_t, 6>mac{};
    unsigned int mtu = 0; 
    bool link_up = false;
}; 

int intf_name_to_index(std::string intf_name);
void intf_index_to_name(int index, char* buf, size_t len);
std::vector<NetworkInterface> discover();
void print(std::vector<NetworkInterface> intf_list);


#endif