#ifndef NETWORK_STATISTICS_H
#define NETWORK_STATISTICS_H

#include <stdint.h>

// these values come from rtnl_link_stats64
// check ethtool_stats when there's non zero errors / packet drops
class NetworkStatistics {
private:
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_missed_errors;
    uint64_t rx_crc_errors;
public:
    NetworkStatistics(
        uint64_t rx_packets, 
        uint64_t tx_packets, uint64_t rx_bytes, 
        uint64_t tx_bytes, uint64_t rx_dropped, 
        uint64_t tx_dropped, uint64_t rx_errors, 
        uint64_t tx_errors, uint64_t rx_missed_errors,
        uint64_t rx_crc_errors
    ) { 
        
    }
    NetworkStatistics(const NetworkStatistics& other) = default;
    NetworkStatistics(NetworkStatistics&& other) = default;
    NetworkStatistics& operator= (const NetworkStatistics& other) = default;
    NetworkStatistics& operator= (NetworkStatistics&& other) = default;
    ~NetworkStatistics() = default;
};

#endif

