#pragma once

#include <chrono>
#include <memory>
#include <thread>

namespace load_balancer {

struct Server {
  using Duration = std::chrono::milliseconds;

  uint32_t id_;
  uint32_t weight_;
  uint32_t cnt_connects_;
  uint32_t total_request_;
  uint32_t total_time_;

  inline static uint32_t next_id_;

  Server(uint32_t weight)
      : id_(next_id_++)
      , weight_(weight) {}

  Duration process(Task task) noexcept {
    ++cnt_connects_;

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(task.time());
    auto end = std::chrono::steady_clock::now();

    --cnt_connects_;
    total_time_ += static_cast<uint64_t>(
        std::chrono::duration_cast<Duration>(end - start).count());
    ++total_request_;
  }

  uint32_t getId() const noexcept {
    return id_;
  }
};

using ServerPtr = std::shared_ptr<Server>;

}  // namespace load_balancer