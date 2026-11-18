#include "helpers.h"
#include <cstdint>
#include <utility>

template <uint32_t packet_size>
class PacketReceiver {
private:
    PacketSocket raw_socket;  
    uint64_t num_packets_received = 0; 
    const uint64_t num_packets_expected = 0; 
    char* buf = 0; 

public:
    PacketReceiver(MacAddress dest_mac_address, int intf_idx, uint64_t num_packets_expected);
    void receive(uint16_t deadline_seconds); 
    std::pair<uint64_t, uint64_t> get();
    ~PacketReceiver();
}; 