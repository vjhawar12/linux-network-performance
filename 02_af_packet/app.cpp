#include "helpers.h"
#include "packet_generator.h"
#include "packet_receiver.h"
#include <iostream>
#include <thread>
#include <array>
#include <cmath>
#include <atomic>

int app(int argc, char *argv[]) {
    if (argc == 9) {
        const char* source_intf_name = argv[1];
        const char* destination_intf_name = argv[2];
        const char* _src_mac = argv[3];
        const char* _dest_mac = argv[4];
        const char* _message = argv[5];
        const char* _bitrate_s = argv[6]; 
        const char* _duration_s = argv[7]; 
        const char* _runid = argv[8];
        results_t results{}; 
        char* endptr1, *endptr2;
        uint64_t bitrate_s = strtoul(_bitrate_s, &endptr1, 10);
        uint64_t duration_s = strtoul(_duration_s, &endptr2, 10);
        char* endptr3;
        uint64_t runid = strtoul(_runid, &endptr3, 10); 
        uint64_t sequence_number = -1; // dummy value, replace this with per-packet ID
        if (errno == ERANGE || endptr1 == _bitrate_s || endptr2 == _duration_s || endptr3 == _runid) {
            std::cerr << "Invalid argument" << std::endl;
            return ERR_ARGS;
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
            std::atomic<bool> generator_finished = false; 
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration_s);
            PacketGenerator<PACKET_SIZE_BYTES> generator(source_mac_address, dest_mac_address, src_idx, payload, bitrate_s, deadline, generator_finished); 
            PacketReceiver<PACKET_SIZE_BYTES> receiver(dest_mac_address, dest_idx, deadline);
            auto start = std::chrono::steady_clock::now(); 
            std::exception_ptr recv_excptr, general_excptr;
            std::thread trecv([&](){
                try {
                    receiver.receive(std::ref(generator_finished));
                } catch (PacketReceiverError& e) {
                    recv_excptr = std::current_exception(); 
                } catch (std::exception& e) {
                    general_excptr = std::current_exception(); 
                }
            }); 
            auto end = std::chrono::steady_clock::now(); 
            try {
                generator.generate(duration_s);
                end = std::chrono::steady_clock::now();
            } catch (PacketGeneratorError& e) {
                std::cerr << "PacketGeneratorError: " << e.what() << std::endl;
                trecv.join();
                return ERR_GENERATE;
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;        
                trecv.join();
                return ERR_UNKNOWN;
            } catch (...) {
                std::cerr << "Unknown exception caught (around generator.generate())!\n";
                return ERR_UNKNOWN;
            }
            results.tx_received = generator.get_tx_received();
            trecv.join();
            if (recv_excptr) {
                std::rethrow_exception(recv_excptr); 
            }
            if (general_excptr) {
                std::rethrow_exception(general_excptr); 
            }
            double real_elapsed_time = std::chrono::duration<double>(end - start).count();
            results.elapsed_time = real_elapsed_time;
            std::tie(results.rx_received, results.tp_drops, results.tp_packets) = receiver.get(); 
            results.throughput_bps = (results.rx_received * PACKET_SIZE_BYTES * 8) / (real_elapsed_time);
            results.lp = results.tx_received > 0 && results.rx_received > 0? 100 * (results.tx_received - results.rx_received) / (double)results.tx_received : 100; 
            std::cout << "Expected Throughput (bits per second): " << std::round(bitrate_s) << std::endl;
            std::cout << "Frames Submitted (sendto): " << results.tx_received << std::endl;
            std::cout << "Frames Read (recv): " << results.rx_received << std::endl;
            std::cout << "Individual Frame Size: " << PACKET_SIZE_BYTES << " bytes" << std::endl;
            std::cout << "Individual Payload size: " << PAYLOAD_SIZE_BYTES << " bytes" << std::endl;
            std::cout << "Elapsed time (seconds): " << real_elapsed_time << std::endl;
            std::cout << "Achieved Throughput (bits per second): " << std::round(results.throughput_bps) << std::endl;
            std::cout << "TP Packets: " << results.tp_packets << std::endl;
            std::cout << "TP Drops: " << results.tp_drops << std::endl;
            std::cout << "Loss percentage: " << results.lp << "%" << std::endl;
        } catch (PacketReceiverError& e) {
            std::cerr << "PacketReceiverError: " << e.what() << std::endl;
            return ERR_RECEIVE;
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl; 
            return ERR_UNKNOWN;
        } catch (...) {
            std::cerr << "Unknown exception caught!\n";
            return ERR_UNKNOWN;
        }
    } else {
        return ERR_ARGS;
    }
    return ERR_NONE;
}       