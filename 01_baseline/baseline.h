#ifndef BASELINE_H
#define BASELINE_H

#include <string>
#include <iostream>
#include <stdint.h>

class InterfaceDiscoveryException : public std::runtime_error {
    using std::runtime_error::runtime_error;
}; 

enum class NetworkEvent : uint8_t {
    InterfaceDiscoveredEvent,
    InterfaceDisconnectedEvent,
    SoftwareEnabledEvent,
    PhysicalLinkDetectedEvent,
    CarrierDetectedEvent,
    PromiscModeDetectedEvent
};

class nw_intf_t {
public:
    int idx;
    std::string name;
    bool software_enabled;
    bool physical_link_up;
    bool carrier_detected;
    bool promisc_mode;

    nw_intf_t(const std::string &_name, bool _software_enabled, bool _physical_link_up, bool _carrier_detected, bool _promisc_mode, int _idx) : idx(_idx), name(std::move(_name)) {
        software_enabled = _software_enabled;
        physical_link_up = _physical_link_up;
        carrier_detected = _carrier_detected;
        promisc_mode = _promisc_mode;
    }

    nw_intf_t(const std::string &_name, int _idx) : idx(_idx), name(std::move(_name)) {
        software_enabled = false;
        physical_link_up = false;
        carrier_detected = false;
        promisc_mode = false;
    }
    
    // network interface object does not hold system resources; allowed to be copyable/moveable
    // copy constructor
    nw_intf_t(const nw_intf_t& other) = default;
    // move constructor
    nw_intf_t(nw_intf_t&& other) = default; 
    // copy assignment operator
    nw_intf_t& operator= (const nw_intf_t& other) = default;
    // move assignment operator 
    nw_intf_t& operator= (nw_intf_t&& other) = default; 
    // used with std::cout
    friend std::ostream& operator<<(std::ostream& os, const nw_intf_t& other) {
        os << "INTF " << other.name  << " IDX " << other.idx << " LINK UP " << (other.physical_link_up? "UP" : "DOWN") 
        << " SW ENABLED " << (other.software_enabled? "TRUE" : "FALSE") << " KERNEL DETECTED " 
        << (other.carrier_detected? "TRUE" : "FALSE") << " PROMISCUOUS " << (other.promisc_mode? "TRUE" : "FALSE") 
        << std::endl;
        return os;
    }

    ~nw_intf_t() = default; 
}; 

void app(void* args) noexcept;

#endif