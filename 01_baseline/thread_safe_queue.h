#ifndef THREAD_SAFE_QUEUE
#define THREAD_SAFE_QUEUE

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

template <typename T, uint16_t capacity>
class ThreadSafeQueue {
private:
    std::array<T, capacity> data{};
    std::atomic<uint16_t> front = 0;
    std::atomic<uint16_t> size = 0; 
    std::atomic<uint16_t> remaining = 0;
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

    void add(T&& elem) {
        if (size < capacity - 1) {
            data[size] = elem;
            size.fetch_add(1, std::memory_order_acquire);
            // size++ cannot move above this line
            size++;
            remaining++;
        }
    }

    void add(const T& elem) {
        if (size < capacity - 1) {
            data[size] = elem;
            size.fetch_add(1, std::memory_order_acquire);
            // size++ cannot move above this line
            size++;
            remaining++;
        }
    }

    std::optional<T> get() {
        if (remaining > 0 && front < size - 1) {
            T temp = data[front];
            // size++ cannot move below this line
            size.fetch_add(1, std::memory_order_release);
            front++;
            remaining--;
            return temp;
        }
        return std::nullopt;
    }

    bool is_empty() {
        return remaining == 0;
    }
};

#endif