/* 
A1: interface discovery and basic information like link up / down, autonegotiation status etc reporting

for network discovery <ifaddrs.h> is a header file which offers os details for networking

A2: process wrapper that launches iperf3 client/server instance and captures/stores iperf runtime statistics in json object
A3: setup namespaces so packets can be routed through the switch and not internally
A4: implement continuous monitoring and track rx/tx counter measurements 
A5: physical verification on hardware
*/

#include "baseline.h"
#include "thread_safe_queue.h"
#include <chrono>
#include <condition_variable>
#include <ifaddrs.h>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <memory>
#include <net/if.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <errno.h>
#include <thread>
#include <utility>

ThreadSafeQueue<std::pair<int, std::pair<NetworkEvent, bool>>, 16> queue;

// these objects let thread block (not sleep) until the condition is true based on the predicate ready 
// and then wake up and acquire the lock. lock guard lets them release mutex automatically 
// once they're out of scope or dead
std::condition_variable cv;
std::mutex mtx;
bool ready;

void discover_interfaces(std::unordered_map<int, std::unique_ptr<nw_intf_t>>& intf_map) {
    ifaddrs* raw_addr; 
    int ret = getifaddrs(&raw_addr);
    if (ret == 0) {
        std::unique_ptr<ifaddrs, void(*)(ifaddrs*)> head(raw_addr, freeifaddrs); 
        ifaddrs* temp = head.get();
        while (temp != nullptr) {
            const char * const name = temp->ifa_name;
            bool software_enabled = temp->ifa_flags & IFF_UP; 
            bool physical_link_up = temp->ifa_flags & IFF_LOWER_UP;
            bool carrier_detected = temp->ifa_flags & IFF_RUNNING;
            bool promisc_mode = temp->ifa_flags & IFF_PROMISC; 
            int idx = if_nametoindex(name); 
            if (idx == 0) {
                std::string s = "Error during interface discovery: errno = " + std::to_string(errno);
                throw InterfaceDiscoveryException(s); 
            } 
            std::cout << "New interface found: " << idx << std::endl;
            intf_map[idx] = std::make_unique<nw_intf_t>(std::string(name), software_enabled, physical_link_up, carrier_detected, promisc_mode, idx); 
            temp = temp->ifa_next;
        } 
        if (intf_map.size() == 0) {
            throw InterfaceDiscoveryException("No network interfaces found!");
        }
    } else {
        throw InterfaceDiscoveryException("Unable to discover interfaces with getifaddrs()!");
    }
}

void print_interfaces(std::unordered_map<int, std::unique_ptr<nw_intf_t>>& intf_map) {
    for (auto it = intf_map.begin(); it != intf_map.end(); it++) {
        std::cout << *(it->second) <<  std::endl;
    }
}

// monitoring real time changes to network interfaces like link up/down status
// using netlink rather than raw system calls to virtual files to interface with the linux subsystem and get notified automatically 
// rather than polling for changes
void monitor_interfaces() {
    // opening netlink socket to kernel
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    // listen for changes in link status
    int optval = RTMGRP_LINK; 
    setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &optval, sizeof(int)); 
    // receiving the raw bytes in buf
    char buf[4096]{};
    while (1) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] {return !ready;}); 
        ssize_t bytes_received = recv(fd, buf, sizeof(buf), MSG_DONTWAIT); 
        if (bytes_received == 0) continue;
        // casting bytes with reinterpret_cast to nlmsghdr to interpret them 
        nlmsghdr* header = reinterpret_cast<nlmsghdr*>(buf); 
        ifinfomsg* temp = NULL; 
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
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::InterfaceDiscoveredEvent, 1)));
                // update state based on flags
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::SoftwareEnabledEvent, (temp->ifi_flags & IFF_UP) != 0)));
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::PhysicalLinkDetectedEvent, (temp->ifi_flags & IFF_LOWER_UP) != 0)));
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::CarrierDetectedEvent, (temp->ifi_flags & IFF_RUNNING) != 0)));
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::PromiscModeDetectedEvent, (temp->ifi_flags & IFF_PROMISC) != 0)));
                ready = true;
                cv.notify_one();
                break;
            case RTM_DELLINK: // called when the interface is disconnecting 
                temp = reinterpret_cast<ifinfomsg*>(buf + sizeof(nlmsghdr)); 
                // ifi_index is the same interface index being used in nw_intf_t
                idx = temp->ifi_index;
                queue.add(std::make_pair(idx, std::make_pair(NetworkEvent::InterfaceDisconnectedEvent, 1)));
                ready = true;
                cv.notify_one();
                break;
            default:
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void app(void* args) noexcept {
    (void)args;
    std::unordered_map<int, std::unique_ptr<nw_intf_t>> nw_intf_list; 
    try {
        discover_interfaces(nw_intf_list);
    } catch (const InterfaceDiscoveryException& e) {
        std::cerr << "Caught: " << e.what() << std::endl; 
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
    }
    std::cout << "Initial Interface Discovery" << std::endl;
    for (const auto& [idx, obj] : nw_intf_list) {
        std::cout << *obj << std::endl;
    }
    // creating thread for get_interface_info
    std::thread interface_monitor(monitor_interfaces);
    interface_monitor.detach();
    int ret;
    // responding to real time events
    while (1) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] {return ready;}); 
        ready = false;
        auto item = queue.get();
        if (item == std::nullopt) continue; 
        int idx = item->first;
        char buff[IF_NAMESIZE]{};  
        NetworkEvent event_type = item->second.first;
        bool event_state = item->second.second;
        switch (event_type) {
            case NetworkEvent::InterfaceDiscoveredEvent:
                if_indextoname(idx, buff); 
                nw_intf_list[idx] = std::make_unique<nw_intf_t>(std::string(buff), idx); 
                break;
            case NetworkEvent::InterfaceDisconnectedEvent:
                ret = nw_intf_list.erase(idx); 
                if (ret <= 0) {
                    std::cerr << "Error during erase!" << std::endl;
                }
                break;
            case NetworkEvent::CarrierDetectedEvent:
                nw_intf_list[idx]->carrier_detected = event_state;
                break;
            case NetworkEvent::SoftwareEnabledEvent:
                nw_intf_list[idx]->software_enabled = event_state;
                break;
            case NetworkEvent::PhysicalLinkDetectedEvent:
                nw_intf_list[idx]->physical_link_up = event_state;
                break;
            case NetworkEvent::PromiscModeDetectedEvent:
                nw_intf_list[idx]->promisc_mode = event_state; 
                break;
            default:
                break;
        }
        print_interfaces(nw_intf_list); 
        lock.~unique_lock(); // releasing the mutex via RAII
    } 
}