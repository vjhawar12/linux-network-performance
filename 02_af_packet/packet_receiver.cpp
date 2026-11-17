#include "helpers.h"
#include <cstdint>
#include <utility>

template <uint32_t packet_size>
class PacketReceiver {
private:
    PacketSocket raw_socket;  
    uint64_t num_packets_received = 0; 
    const uint64_t num_packets_expected = 0; 
    char* buf = 0; 

public:
    PacketReceiver(
        MacAddress dest_mac_address, 
        int intf_idx,
        timeval tv
    ) : 
        raw_socket(
        dest_mac_address, 
        intf_idx, 
        SOL_SOCKET,
        SO_RCVTIMEO,
        &tv,
        sizeof(timeval)
    ), num_packets_expected(
        num_packets_expected
    )  {
        buf = new char[num_packets_expected]; 
    }

    void receive() {
        size_t bytes_recvd = 0;
        while (num_packets_received < num_packets_expected) {
            bytes_recvd = recv(raw_socket.get_fd(), buf + num_packets_received, packet_size, 0); 
            if (bytes_recvd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("Receive timed out.\n");
                } else {
                    perror("recv error");
                }
                continue;
            } 
            num_packets_received++;
        }
    }

    std::pair<uint64_t, uint64_t> get() {
        return std::make_pair(num_packets_expected, num_packets_received); 
    }

    ~PacketReceiver() {
        delete[] buf;
    }
}; 