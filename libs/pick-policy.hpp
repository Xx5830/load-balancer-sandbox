#pragma once

#include <exception>
#include <optional>
#include <stdexcept>

#include "server.hpp"

namespace load_balancer {
struct IPickPolicy {
   protected:
    std::vector<ServerPtr>* servers_;

   public:
    virtual std::optional<ServerPtr> pickServer() = 0;
    IPickPolicy(std::vector<ServerPtr>* servers)
        : servers_(servers) {}

    virtual ~IPickPolicy() {};
};

struct RoundRobinPick : IPickPolicy {
   protected:
    size_t index_;

   public:
    RoundRobinPick(std::vector<ServerPtr>* servers)
        : IPickPolicy(servers)
        , index_(0) {}

    std::optional<ServerPtr> pickServer() {
        if (servers_->empty()) {
            return std::nullopt;
        } else if (index_ >= servers_->size()) {
            // не меняйте на минус, количество серверов может уменьшиться
            index_ %= servers_->size();
        }

        return (*servers_)[index_++];
    }
};

struct WeightRoundRobinPick : IPickPolicy {
   protected:
    size_t index_;
    size_t cnt_;

   public:
    WeightRoundRobinPick(std::vector<ServerPtr>* servers)
        : IPickPolicy(servers)
        , index_(-1)
        , cnt_(0) {}

    std::optional<ServerPtr> pickServer() {
        if (servers_->empty()) {
            return std::nullopt;
        }

        if (cnt_ == 0) {
            ++index_;
            if (index_ >= servers_->size()) {
                // не меняйте на минус, количество серверов может уменьшиться
                index_ %= servers_->size();
            }
            cnt_ = (*servers_)[index_]->getWeight();
            if (cnt_ == 0) {
                // временная проверка, перенести в конструктор сервера
                throw std::runtime_error("Запрещено иметь вес меньше 1");
            }
        }

        return (*servers_)[index_];
    }
};

}  // namespace load_balancer