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

#include <cstdint>
#include <net/ethernet.h> /* the L2 protocols */
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <unistd.h>
#include "helpers.h"
#include <atomic>

template <uint32_t packet_size>
class PacketGenerator {
private:
    EthernetFrame<packet_size> frame{};
    PacketSocket raw_socket;  
    uint64_t bitrate_s = 0;
    std::chrono::steady_clock::time_point deadline{}; 
    uint64_t transmitted_packets = 0;
    std::atomic<bool>& finished;
public:
    PacketGenerator(
        MacAddress source_mac_address, 
        MacAddress dest_mac_address, 
        int intf_idx, 
        std::array<uint8_t, packet_size - 14>& custom_payload,
        uint64_t bitrate_s,
        std::chrono::steady_clock::time_point deadline,
        std::atomic<bool>& finished
    ); 
    void generate(uint32_t duration_s);
    unsigned long get_tx_received();
};
