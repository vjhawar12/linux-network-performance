#include "helpers.h"
#include "packet_generator.h"
#include "packet_receiver.h"
#include <iostream>
#include <thread>

int app(int argc, char *argv[]) {
    if (argc == 8) {
        const char* source_intf_name = argv[1];
        const char* destination_intf_name = argv[2];
        const char* _src_mac = argv[3];
        const char* _dest_mac = argv[4];
        const char* _message = argv[5];
        const char* _num_packets = argv[6]; 
        const char* _runid = argv[7];
        char* endptr;
        uint64_t num_packets = strtoul(_num_packets, &endptr, 10);
        char* endptr2;
        uint64_t runid = strtoul(_runid, &endptr2, 10); 
        uint64_t sequence_number = -1; // dummy value, replace this with per-packet ID
        if (errno == ERANGE || endptr == _num_packets) {
            std::cerr << "Invalid argument for num_packets: " << _num_packets << std::endl;
            return 1;
        }
        int src_idx = intf_name_to_index(source_intf_name); 
        int dest_idx = intf_name_to_index(destination_intf_name); 
        try {
            MacAddress dest_mac_address(_dest_mac); 
            MacAddress source_mac_address(_src_mac); 
            // a submitted frame should look like this Ethernet header (14 bytes) | Run ID (8 bytes) | Sequence number (8 bytes) | [custom message e.g. "hello"]
            // the payload supplied by the user via argv should be just the custom message, and the program should append the Run ID and Sequence Number
            uint8_t payload[242]{};
            uint8_t message[226]{};
            std::memcpy(message, _message, strnlen(_message, 226)); 
            std::memcpy(payload, &runid, sizeof(runid)); 
            std::memcpy(payload + 8, &sequence_number, sizeof(sequence_number)); 
            std::memcpy(payload + 16, message, 226); 
            PacketGenerator<256> generator(source_mac_address, dest_mac_address, src_idx, payload, num_packets); 
            PacketReceiver<256> receiver(dest_mac_address, dest_idx, num_packets); 
            std::thread trecv(&PacketReceiver<256>::receive, &receiver, 10); // 10 second deadline
            try {
                generator.generate();
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;        
                trecv.join();
                return 1;
            }
            trecv.join();
            std::cout << "Expected: " << receiver.get().first << " Received: " << receiver.get().second << std::endl;
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl; 
            return 1;
        } 
    } else {
        return 1;
    }
    return 0;
}   