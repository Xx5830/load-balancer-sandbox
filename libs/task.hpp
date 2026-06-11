#pragma once

#include <cstdint>
#include <future>

namespace load_balancer {

using Duration = std::chrono::milliseconds;

class Task {
   public:
    Task()
        : id_(0)
        , cost_(0.0) {}

    Task(uint64_t id, long double cost)
        : id_(id)
        , cost_(cost) {}

    uint64_t getId() const {
        return id_;
    }

    void setId(uint64_t id) {
        id_ = id;
    }

    long double getCost() const {
        return cost_;
    }

    void setCost(long double cost) {
        cost_ = cost;
    }

   private:
    uint64_t id_;
    long double cost_;
};

struct TaskItem {
    Task task;
    std::promise<Duration> promise;
};

}  // namespace load_balancer
