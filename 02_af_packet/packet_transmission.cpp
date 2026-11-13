// packet transmission with raw sockets to bypass the network stack but not the kernel yet

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <iostream>

int intf_idx(std::string&& intf_name) {
    int idx = if_nametoindex(intf_name.c_str());
    if (idx == 0) {
        std::cerr << "Could not get interface index for " << intf_name << std::endl; 
        return -1; 
    }
    return idx; 
}

int main() {
    // opening raw socket
    int fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (fd == -1) {
        // check errno
        return 1;
    } 
    int idx = intf_idx("eth0"); 
    char custom_payload[256] = {0}; 
    size_t len = sizeof(custom_payload); 
    if (idx > 0) {
        const sockaddr_in dest = {
            
        }; 
        socklen_t socklen = sizeof(dest); 
        int ret = sendto(fd, custom_payload, len, MSG_CONFIRM, reinterpret_cast<const sockaddr*>(&dest), socklen); 
        if (ret == -1) {
            // check errno
            return 1;
        }
    }
    return 1;
}   