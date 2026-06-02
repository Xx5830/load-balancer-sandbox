#pragma once

#include <exception>
#include <optional>

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
            index_ -= servers_->size();
        }

        return (*servers_)[index_++];
    }
};

}  // namespace load_balancer