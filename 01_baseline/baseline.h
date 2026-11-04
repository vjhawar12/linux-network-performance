#ifndef BASELINE_H
#define BASELINE_H

#include <string>
#include <iostream>

class InterfaceDiscoveryException : public std::runtime_error {
    using std::runtime_error::runtime_error;
}; 

class nw_intf_t {
public:
    static int length; 
    const char * const name;
    bool software_enabled;
    bool physical_link_up;
    bool is_running;
    bool promisc_mode;

    nw_intf_t(const char * const _name, bool _software_enabled, bool _physical_link_up, bool _is_running, bool _promisc_mode) : name(_name) {
        software_enabled = _software_enabled;
        physical_link_up = _physical_link_up;
        is_running = _is_running;
        promisc_mode = _promisc_mode;
    }
    
    // network interface object should not be copyable or moveable
    nw_intf_t(const nw_intf_t& other) = delete; 
    // copy assignment operator
    nw_intf_t& operator= (const nw_intf_t& other) = delete;
    // move assignment operator 
    nw_intf_t& operator= (nw_intf_t&& other) = delete; 

    friend std::ostream& operator<<(std::ostream& os, const nw_intf_t& other) {
        os << "INTF " << other.name << " PHYSICAL LINK " << (other.physical_link_up? "UP" : "DOWN") 
        << " SW ENABLED " << (other.software_enabled? "TRUE" : "FALSE") << " KERNEL DETECTED " 
        << (other.is_running? "TRUE" : "FALSE") << " PROMISCUOUS " << (other.promisc_mode? "TRUE" : "FALSE") 
        << std::endl;
        return os;
    }

    ~nw_intf_t() {
        
    }
}; 

int app(void* args) noexcept;

#endif