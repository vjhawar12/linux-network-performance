#ifndef THREAD_SAFE_QUEUE
#define THREAD_SAFE_QUEUE

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

template <typename T, uint16_t capacity>
// Note: Currently thread safe only for single producer/consumer, not yet for multi producer and multi consumer
class ThreadSafeQueue {
private:
    std::array<T, capacity> data{};
    std::atomic<uint16_t> front = 0;
    std::atomic<uint16_t> back = 0;

public:
    ThreadSafeQueue() = default;
    // copy constructor
    ThreadSafeQueue(const ThreadSafeQueue& other) = default;
    // move constructor
    ThreadSafeQueue(ThreadSafeQueue&& other) = default; 
    // copy assignment operator
    ThreadSafeQueue& operator= (const ThreadSafeQueue& other) = default;
    // move assignment operator 
    ThreadSafeQueue& operator= (ThreadSafeQueue&& other) = default; 
    ~ThreadSafeQueue() = default; 
    
    int get_size() {
        int _back = back.load(std::memory_order_acquire);
        int _front = front.load(std::memory_order_acquire);
        return _back - _front;
    }

    void add(T&& elem) {
        if (!is_full()) {
            uint16_t current_back = back.load(std::memory_order_relaxed); 
            data[current_back % capacity] = std::move(elem);
            back.fetch_add(1, std::memory_order_release);
        }
    }

    void add(const T& elem) {
        if (!is_full()) {
            uint16_t current_back = back.load(std::memory_order_relaxed); 
            data[current_back % capacity] = elem;
            back.fetch_add(1, std::memory_order_release);
        }
    }

    std::optional<T> get() {
        // retrieve the element at the front of the queue
        // increment the front 
        if (!is_empty()) {
            uint16_t current_front = front.load(std::memory_order_relaxed); 
            T temp = std::move(data[current_front % capacity]);
            front.fetch_add(1, std::memory_order_release);
            return temp;
        }
        return std::nullopt;
    }

    bool is_empty() {
        return get_size() == 0;
    }

    bool is_full() {
        return get_size() == capacity; 
    }
};

#endif