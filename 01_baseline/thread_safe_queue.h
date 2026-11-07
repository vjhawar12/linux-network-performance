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
    // internal array for holding elements
    std::array<T, capacity> data{};
    // front and back pointers for the queue, both atomic
    std::atomic<uint16_t> front = 0;
    std::atomic<uint16_t> back = 0;

public:
    // default constructor 
    ThreadSafeQueue() = default;
    // copy constructor
    ThreadSafeQueue(const ThreadSafeQueue& other) = default;
    // move constructor
    ThreadSafeQueue(ThreadSafeQueue&& other) = default; 
    // copy assignment operator
    ThreadSafeQueue& operator= (const ThreadSafeQueue& other) = default;
    // move assignment operator 
    ThreadSafeQueue& operator= (ThreadSafeQueue&& other) = default; 
    // destructor
    ~ThreadSafeQueue() = default; 
    
    // return the current size (not capacity) of the queue 
    uint16_t get_size() {
        // using acquire so the subtraction does not happen on stale values
        uint16_t _back = back.load(std::memory_order_acquire);
        uint16_t _front = front.load(std::memory_order_acquire);
        return _back - _front;
    }

    // add an element to the back of the queue with rvalue reference, return nullopt if queue full else true.
    bool add(T&& elem) {
        if (!is_full()) {
            // data[] = std::move(elem) depends on loading 'back' so that line couldn't be reordered anyways, so relaxed works
            uint16_t current_back = back.load(std::memory_order_relaxed); 
            data[current_back % capacity] = std::move(elem);
            // do not want to increment back before the array access, therefore use release
            back.fetch_add(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    // add an element to the back of the queue with const lvalue reference, return nullopt if queue full else true.
    bool add(const T& elem) {
        if (!is_full()) {
            uint16_t current_back = back.load(std::memory_order_relaxed); 
            data[current_back % capacity] = elem;
            back.fetch_add(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    // retrieve the element at the front of the queue by value, return nullopt if queue empty else true.
    // returning by reference could have multiple threads pointing to the same copy, by value is expensive 
    // but thread safe
    std::optional<T> get() {
        // retrieve the element at the front of the queue
        // increment the front 
        if (!is_empty()) {
            // load the snapshot of front before incrementing
            uint16_t current_front = front.load(std::memory_order_relaxed);
            // perform array access with std::move to pop element off by move constructor 
            T temp = std::move(data[current_front % capacity]);
            // increment front only after element was popped, therefore use release
            front.fetch_add(1, std::memory_order_release);
            return temp;
        }
        return std::nullopt;
    }

    // empty: size (not capacity) = 0
    bool is_empty() {
        return get_size() == 0;
    }

    // full: size == capacity
    bool is_full() {
        return get_size() == capacity; 
    }
};

#endif