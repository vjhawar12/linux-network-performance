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
    char* buf = 0; 
    std::chrono::steady_clock::time_point deadline;
    uint32_t tp_packets = 0;
    uint32_t tp_drops = 0;
    uint32_t timeouts_ = 0;
    uint32_t timeout_ms = 20;
    void _receive(std::chrono::steady_clock::time_point _deadline, std::function<bool()>&& cond);

public:
    PacketReceiver(MacAddress dest_mac_address, int intf_idx, std::chrono::steady_clock::time_point deadline);
    void receive(std::atomic<bool>& generator_finished); 
    std::tuple<uint64_t, uint32_t, uint32_t> get();
    ~PacketReceiver();
}; 