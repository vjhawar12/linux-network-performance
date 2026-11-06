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
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <errno.h>
#include <thread>

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
            bool physical_link_up = temp->ifa_flags & IFF_LOWER_UP;
            bool is_running = temp->ifa_flags & IFF_RUNNING;
            bool promisc_mode = temp->ifa_flags & IFF_PROMISC; 
            int idx = if_nametoindex(name); 
            if (idx == 0) {
                std::string err = "No interface found: errno = " + std::to_string(errno);
                throw InterfaceDiscoveryException(err);
            } else if (nw_intf_list.find(idx) != nw_intf_list.end()) {
                nw_intf_list[idx] = std::make_unique<nw_intf_t>(std::string(name), software_enabled, physical_link_up, is_running, promisc_mode, idx); 
            }
            temp = temp->ifa_next;
        } 
        if (nw_intf_list.size() == 0) {
            throw InterfaceDiscoveryException("No network interfaces found!");
        }
    } else {
        throw InterfaceDiscoveryException("Unable to discover interfaces with getifaddrs()!");
    }
}
// monitoring real time changes to network interfaces like link up/down status
// using netlink rather than raw system calls to virtual files to interface with the linux subsystem and get notified automatically 
// rather than polling for changes
void get_interface_info() {
    // opening netlink socket to kernel
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    // listen for changes in link status
    int optval = RTMGRP_LINK; 
    setsockopt(fd, SOL_SOCKET, NETLINK_ADD_MEMBERSHIP, &optval, sizeof(int)); 
    // receiving the raw bytes in buf
    char buf[4096]; 
    ssize_t bytes_received = recv(fd, buf, sizeof(buf), MSG_DONTWAIT); 
    // casting bytes with reinterpret_cast to nlmsghdr to interpret them 
    nlmsghdr *header = reinterpret_cast<nlmsghdr*>(buf); 
    ifinfomsg* temp = NULL; 
    bool new_interface_found = false;
    int idx = -1;
    // parsing header
    switch (header->nlmsg_type) {
        case NLMSG_DONE:
            break;
        case NLMSG_ERROR:
            break;
        case RTM_NEWLINK:
            temp = reinterpret_cast<ifinfomsg*>(buf + sizeof(nlmsghdr)); 
            // ifi_index is the same interface index being used in nw_intf_t
            idx = temp->ifi_index;
            if (nw_intf_list.find(idx) == nw_intf_list.end()) {
                char* buff;
                if_indextoname(idx, buff);
                nw_intf_list[idx] = std::make_unique<nw_intf_t>(std::string(buff), false, false, false, false, idx); 
            }
            // update state based on flags
            nw_intf_list[idx]->software_enabled = temp->ifi_flags & IFF_UP; 
            nw_intf_list[idx]->physical_link_up = temp->ifi_flags & IFF_LOWER_UP;
            nw_intf_list[idx]->is_running = temp->ifi_flags & IFF_RUNNING;
            nw_intf_list[idx]->promisc_mode = temp->ifi_flags & IFF_PROMISC; 
            break;
        case RTM_DELLINK: // called when the interface is disconnecting 
            temp = reinterpret_cast<ifinfomsg*>(buf + sizeof(nlmsghdr)); 
            // ifi_index is the same interface index being used in nw_intf_t
            idx = temp->ifi_index;
            // remove interface from map
            if (nw_intf_list.erase(idx) == 1) {

            } 
            break;
        default:
            break;
    }
}

int app(void* args) noexcept {
    (void*)args;
    try {
        discover_interfaces();
    } catch (const InterfaceDiscoveryException& e) {
        std::cerr << "Caught: " << e.what() << std::endl;
        return 1; 
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
    }
    for (const auto& [idx, obj] : nw_intf_list) {
        std::cout << *obj << std::endl;
    }
    // creating thread for get_interface_info
    std::thread interface_monitor(get_interface_info);
    interface_monitor.join();
    return 0;
}