#pragma once

#include <chrono>
#include <cstddef>

namespace load_balancer {

class Task {
   public:
    using duration = std::chrono::steady_clock::duration;

    Task(size_t id, long double cost, duration execution_time)
        : id_(id)
        , cost_(cost)
        , execution_time_(execution_time) {}

    duration time() const {
        return execution_time_;
    }

    void setTime(duration time) {
        execution_time_ = time;
    }

    void setId(size_t id) {
        this->id_ = id;
    }

    size_t getId() const {
        return id_;
    }

    void setCost(long double cost) {
        this->cost_ = cost;
    }

    long double getCost() const {
        return cost_;
    }

   private:
    size_t id_;
    long double cost_;
    duration execution_time_;
};

}  // namespace load_balancer
