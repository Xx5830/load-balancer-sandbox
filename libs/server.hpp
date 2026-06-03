#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "task.hpp"

namespace load_balancer {

struct Server {
    using Duration = std::chrono::milliseconds;

    uint64_t id_;
    uint32_t weight_;  // запрещено меньше 1, проконтролируйте
    std::atomic<uint32_t> cnt_connects_{0};
    std::atomic<uint64_t> total_request_{0};
    std::atomic<uint64_t> total_time_{0};

    inline static std::atomic<uint64_t> next_id_{0};

    Server(uint32_t weight)
        : id_(next_id_++)
        , weight_(weight)
        , cnt_connects_(0)
        , total_request_(0)
        , total_time_(0) {}

    Duration runTask(Task task) noexcept {
        cnt_connects_.fetch_add(1, std::memory_order_relaxed);

        auto start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(task.time());
        auto end = std::chrono::steady_clock::now();

        cnt_connects_.fetch_add(-1, std::memory_order_relaxed);
        auto diff_time = std::chrono::duration_cast<Duration>(end - start);
        total_time_ += static_cast<uint64_t>(diff_time.count());
        total_request_.fetch_add(1, std::memory_order_relaxed);

        return diff_time;
    }

    uint64_t getId() const noexcept {
        return id_;
    }

    uint32_t getWeight() const noexcept {
        return weight_;
    }

    uint32_t getConnects() const noexcept {
        return cnt_connects_.load(std::memory_order_relaxed);
    }
};

using ServerPtr = std::shared_ptr<Server>;

}  // namespace load_balancer