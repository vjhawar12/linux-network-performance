#include "Scheduler.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

void Scheduler::init() {
    if (!Scheduler::_init) {
        Scheduler::_init = true;
        std::make_heap(tasks.begin(), tasks.end()); 
    }
}

std::optional<bool> Scheduler::schedule(task_t task, void* args) {
    if (tasks.size() < MAX_SIZE) {
        tasks.push_back(task);
        return true;
    }
    return std::nullopt;
}

void Scheduler::update_state() {
    std::push_heap(tasks.begin(), tasks.end(), compare);
}

void Scheduler::run() {
    update_state();
    auto task = tasks[0];
    if (task.pred(task.args)) {
        task.last_timestamp = std::chrono::high_resolution_clock::now();
        task(); 
    }
}