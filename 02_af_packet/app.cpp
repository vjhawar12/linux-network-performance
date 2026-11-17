#include "helpers.h"
#include "packet_generator.h"
#include "packet_receiver.h"
#include <iostream>

int app(int argc, char *argv[]) {
    if (argc == 6) {
        const char* source_intf_name = argv[1];
        const char* _src_mac = argv[2];
        const char* _dest_mac = argv[3];
        uint8_t* payload = (uint8_t*)argv[4];
        const char* _num_packets = argv[5]; 
        char* endptr;
        uint64_t num_packets = strtoul(_num_packets, &endptr, 10);
        if (errno == ERANGE || endptr == _num_packets) {
            std::cerr << "Invalid argument for num_packets: " << _num_packets << std::endl;
            return 1;
        }
        int idx = intf_name_to_index(source_intf_name); 
        timeval tv;
        tv.tv_usec = 10;
        try {
            MacAddress dest_mac_address(_dest_mac); 
            MacAddress source_mac_address(_src_mac); 
            PacketGenerator<256> gen(source_mac_address, dest_mac_address, idx, payload, num_packets); 
            PacketReceiver<256> receiver(dest_mac_address, idx, tv, num_packets); 
            gen.generate(); 
            receiver.receive();
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