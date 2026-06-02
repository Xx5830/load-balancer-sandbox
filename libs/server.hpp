#pragma once

#include <chrono>
#include <memory>
#include <thread>

#include "task.hpp"

namespace load_balancer {

struct Server {
    using Duration = std::chrono::milliseconds;

    uint32_t id_;
    uint32_t weight_;  // запрещено меньше 1, проконтролируйте
    uint32_t cnt_connects_;
    uint32_t total_request_;
    uint32_t total_time_;

    inline static uint32_t next_id_;

    Server(uint32_t weight)
        : id_(next_id_++)
        , weight_(weight) {}

    Duration runTask(Task task) noexcept {
        ++cnt_connects_;

        auto start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(task.time());
        auto end = std::chrono::steady_clock::now();

        --cnt_connects_;
        auto diff_time = std::chrono::duration_cast<Duration>(end - start);
        total_time_ += static_cast<uint64_t>(diff_time.count());
        ++total_request_;

        return diff_time;
    }

    uint32_t getId() const noexcept {
        return id_;
    }

    uint32_t getWeight() const noexcept {
        return weight_;
    }
};

using ServerPtr = std::shared_ptr<Server>;

}  // namespace load_balancer