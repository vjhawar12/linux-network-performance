/* 
A1: interface discovery and basic information like link up / down, autonegotiation status etc reporting

for network discovery <ifaddrs.h> is a header file which offers os details for networking

A2: process wrapper that launches iperf3 client/server instance and captures/stores iperf runtime statistics in json object
A3: setup namespaces so packets can be routed through the switch and not internally
A4: implement continuous monitoring and track rx/tx counter measurements 
A5: physical verification on hardware
*/

#include "baseline.h"
#include <ifaddrs.h>
#include <unordered_map>
#include <memory>
#include <net/if.h>
#include <linux/if.h>

std::unordered_map<int, std::unique_ptr<nw_intf_t>> nw_intf_list; 

void discover_interfaces() {
    ifaddrs* raw_addr; 
    int ret = getifaddrs(&raw_addr);
    if (ret == 0) {
        std::unique_ptr<ifaddrs, void(*)(ifaddrs*)> head(raw_addr, freeifaddrs); 
        ifaddrs* temp = head.get();
        while (temp != nullptr) {
            const char * const name = temp->ifa_name;
            bool software_enabled = temp->ifa_flags & IFF_UP; 
            bool physical_link_enabled = temp->ifa_flags & IFF_LOWER_UP;
            bool is_running = temp->ifa_flags & IFF_RUNNING;
            bool promisc_mode = temp->ifa_flags & IFF_PROMISC; 
            int idx = if_nametoindex(name); 
            nw_intf_list[idx] = std::make_unique<nw_intf_t>(name, software_enabled, physical_link_enabled, is_running, promisc_mode, idx); 
            temp = temp->ifa_next;
        } 
        if (nw_intf_list.size() == 0) {
            throw InterfaceDiscoveryException("No network interfaces found!");
        }
    } else {
        throw InterfaceDiscoveryException("Unable to discover interfaces with getifaddrs()!");
    }
}

int app(void* args) noexcept {
    (void*)args;
    try {
        discover_interfaces();
    } catch (const InterfaceDiscoveryException& e) {
        std::cout << "Caught: " << e.what() << std::endl;
        return 1; 
    }
    for (const auto& [idx, obj] : nw_intf_list) {
        std::cout << *obj << std::endl;
    }
    return 0;
}