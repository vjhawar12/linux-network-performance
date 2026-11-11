#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <vector>
#include <thread>
#include <time.h>

#define MAX_SIZE 32

class task_t {
public:
    std::function<int(void*)> func;
    std::function<bool(void*)> pred;
    void* args;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_timestamp;

    task_t(std::function<int(void*)> func, void* args, std::function<bool(void*)> pred) {
        this->func = func;
        this->args = args;
        this->pred = pred;
        last_timestamp = std::chrono::high_resolution_clock::now(); 
    }

    int operator()() const {
        return func(args); 
    }    
}; 

static auto compare = [](const task_t& a, const task_t& b) {
    return a.last_timestamp < b.last_timestamp; 
}; 

class Scheduler {
private:
    static std::vector<task_t> tasks;
    static bool _init; 

public:
    Scheduler() = delete;
    Scheduler(const Scheduler& other) = delete;
    Scheduler(Scheduler&& other) = delete;
    Scheduler& operator= (const Scheduler& other) = delete;
    Scheduler& operator= (Scheduler&& other) = delete; 
    ~Scheduler() = delete; 

    static void init();

    static std::optional<bool> schedule(task_t task, void* args);

    static void update_state();
    static void run();
}; 

#endif