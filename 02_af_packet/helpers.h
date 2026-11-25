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
#include <stdexcept>
#include <chrono>

#define ETHERNET_HEADER_BYTES 14
#define RUN_ID_BYTES 8
#define SEQUENCE_NUMBER_BYTES 8
#define PACKET_SIZE_BYTES 1490
#define PAYLOAD_SIZE_BYTES (PACKET_SIZE_BYTES - ETHERNET_HEADER_BYTES)
#define MESSAGE_SIZE_BYTES (PACKET_SIZE_BYTES - ETHERNET_HEADER_BYTES - RUN_ID_BYTES - SEQUENCE_NUMBER_BYTES)
// return true if deadline elapsed else false
#define ELAPSED_MS(now_, deadline_) ((std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now_).count()) <= 0)
#define ELAPSED_1MS(now_) (ELAPSED_MS(now_, now_ + std::chrono::milliseconds(1)))
#define TIME_LEFT_MS(now_, deadline_) (std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now_).count())

typedef enum err_t {
    ERR_NONE,
    ERR_ARGS,
    ERR_GENERATE,
    ERR_RECEIVE,
    ERR_UNKNOWN
} err_t;

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
    const char* get() const;
    void set_sequence_number(uint64_t num); 
    size_t length() const; 
}; 


class PacketGeneratorError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class PacketReceiverError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class PacketSocket {
private:
    int fd = -1;
    int intf_idx; 
    sockaddr_ll dest = {};
    socklen_t socklen = sizeof(dest); 
public:
    PacketSocket(const MacAddress& dest_mac_address, int intf_idx);
    PacketSocket(
        const MacAddress& dest_mac_address, int intf_idx, 
        int sockopt, int level, void* optvalue, size_t optlen
    ); 
    // block copy constructor / assignment operator because that lets double-close on the same fd be possible
    PacketSocket(const PacketSocket& other) = delete;
    PacketSocket& operator= (const PacketSocket& other) = delete;
    // the move constructor / assignment operator is supported 
    // however the default move constructor / assignment operator won't invalidate the old fd
    // which could let a double-close on the same fd happen
    PacketSocket(PacketSocket&& other) noexcept;
    PacketSocket& operator= (PacketSocket&& other) noexcept;
    void bind_socket(); 
    int get_fd() const;
    socklen_t get_socklen() const;
    const sockaddr_ll* get_sockaddr() const;
    ~PacketSocket();
}; 

typedef struct NetworkInterface {
    std::string intf_name{};
    size_t intf_idx = 0; 
    std::array<uint8_t, 6>mac{};
    unsigned int mtu = 0; 
    bool link_up = false;
} NetworkInterface; 

typedef struct results_t {
    unsigned long rx_expected;
    unsigned long rx_received;
    unsigned long tx_expected;
    unsigned long tx_received;
    uint32_t tp_drops;
    uint32_t tp_packets;
    double throughput_bps;
    double elapsed_time;
    double lp;
} results_t;

int intf_name_to_index(std::string intf_name);
void intf_index_to_name(int index, char* buf, size_t len);
std::vector<NetworkInterface> discover();
void print(std::vector<NetworkInterface> intf_list);
void high_performance_busy_wait(uint32_t time_ms);
void high_performance_busy_wait(std::chrono::steady_clock::time_point deadline); 

#endif