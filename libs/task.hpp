#pragma once

#include <cstdint>

namespace load_balancer {

class Task {
   public:
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

}  // namespace load_balancer
