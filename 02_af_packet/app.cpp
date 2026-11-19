#include "helpers.h"
#include "packet_generator.h"
#include "packet_receiver.h"
#include <iostream>
#include <thread>
#include <array>
#include <cmath>

results_t results{}; 

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
        results.tx_expected = num_packets;
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
            std::array<uint8_t, PAYLOAD_SIZE_BYTES> payload; 
            uint8_t message[MESSAGE_SIZE_BYTES]{};
            std::memcpy(message, _message, strnlen(_message, MESSAGE_SIZE_BYTES)); 
            std::memcpy(payload.data(), &runid, sizeof(runid)); 
            std::memcpy(payload.data() + RUN_ID_BYTES, &sequence_number, sizeof(sequence_number)); 
            std::memcpy(payload.data() + RUN_ID_BYTES + SEQUENCE_NUMBER_BYTES, message, MESSAGE_SIZE_BYTES); 
            PacketGenerator<PACKET_SIZE_BYTES> generator(source_mac_address, dest_mac_address, src_idx, payload, num_packets); 
            PacketReceiver<PACKET_SIZE_BYTES> receiver(dest_mac_address, dest_idx, num_packets);
            auto start = std::chrono::steady_clock::now(); 
            std::thread trecv(&PacketReceiver<PACKET_SIZE_BYTES>::receive, &receiver, 10); // 10 second deadline
            try {
                generator.generate();
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;        
                trecv.join();
                return 1;
            }
            results.tx_received = generator.get_tx_received();
            trecv.join();
            auto end = std::chrono::steady_clock::now();
            double elapsed_time = std::chrono::duration<double>(end - start).count();
            results.elapsed_time = elapsed_time;
            results.rx_expected = receiver.get().first;
            results.rx_received = receiver.get().second;
            results.throughput_bps = (results.rx_received * PAYLOAD_SIZE_BYTES * 8) / (elapsed_time);
            std::cout << "Frames Requested: " << num_packets << std::endl;
            std::cout << "Frames Submitted (sendto): " << results.tx_received << std::endl;
            std::cout << "Frames Read (recv): " << results.rx_received << std::endl;
            std::cout << "Individual Frame Size: " << PACKET_SIZE_BYTES << " Individual Payload size: " << PAYLOAD_SIZE_BYTES << std::endl;
            std::cout << "Elapsed time (seconds): " << elapsed_time << std::endl;
            std::cout << "RX Payload Throughput (bits per second): " << std::round(results.throughput_bps) << std::endl;
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl; 
            return 1;
        } 
    } else {
        return 1;
    }
    return 0;
}       