#pragma once

#include <memory>
#include <type_traits>

#include "pick-policy.hpp"
#include "server.hpp"
#include "task.hpp"

namespace load_balancer {
struct LoadBalancer {
    std::unique_ptr<IPickPolicy> pick_policy_;

    template <typename PickPolicy, typename... Args>
        requires std::is_base_of_v<IPickPolicy, PickPolicy>
    void setPickPolicy(Args... args) {
        pick_policy_ = std::make_unique<PickPolicy>(std::forward<Args>(args)...);
    }

    ServerPtr pickServer(uint64_t id) {
        if (!pick_policy_)
            return nullptr;

        auto result = pick_policy_->pickServer(id);
        if (result.has_value()) {
            return *result;
        } else {
            return nullptr;
        }
    }

    void setServers(std::vector<ServerPtr>* servers) {
        if (pick_policy_) {
            pick_policy_->setServers(servers);
        }
    }

    void addServerEvent(ServerPtr server) {
        pick_policy_->addServerEvent(server);
    }
    void eraseServerEvent(ServerPtr server) {
        pick_policy_->eraseServerEvent(server);
    }
};
}  // namespace load_balancer