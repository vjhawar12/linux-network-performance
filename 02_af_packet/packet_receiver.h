#include "helpers.h"
#include <cstdint>
#include <utility>
#include <atomic>
#include <poll.h>
#include <chrono>
#include <functional>

template <uint32_t packet_size>
class PacketReceiver {
private:
    PacketSocket raw_socket;  
    uint64_t num_packets_received = 0; 
    const uint64_t num_packets_expected = 0; 
    char* buf = 0; 
    uint16_t duration_s = 0;
    uint32_t tp_packets;
    uint32_t tp_drops;
    void _receive(pollfd& _pollfd, std::chrono::steady_clock::time_point _deadline, std::function<bool()>&& cond);

public:
    PacketReceiver(MacAddress dest_mac_address, int intf_idx, uint64_t num_packets_expected, uint16_t duration_s);
    void receive(std::atomic<bool>& generator_finished); 
    std::tuple<uint64_t, uint64_t, uint32_t, uint32_t> get();
    ~PacketReceiver();
}; 