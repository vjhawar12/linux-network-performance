#ifndef THREAD_SAFE_QUEUE
#define THREAD_SAFE_QUEUE

#include <array>
#include <atomic>
#include <optional>
#include "baseline.h"

template <typename T, size_t capacity>
class ThreadSafeQueue {
private:
    std::array<T, capacity> data{};
    std::atomic<int> front = 0;
    std::atomic<int> size = 0; 
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
            // use std::memory_order_acquire here 
            data[size++] = elem;
        }
    }

    void add(const T& elem) {
        if (size < capacity - 1) {
            // use std::memory_order_acquire here 
            data[size] = elem;
            size++;
        }
    }

    std::optional<T> get() {
        if (front < size - 1) {
            // use std::memory_order_release here 
            T temp = data[front];
            front++;
            return temp;
        }
        return std::nullopt;
    }
};

#endif