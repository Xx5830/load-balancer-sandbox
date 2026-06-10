#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "pick-policy.hpp"
#include "server.hpp"

namespace load_balancer {

struct LoadBalancer {
    template <typename PickPolicy, typename... Args>
        requires std::is_base_of_v<IPickPolicy, PickPolicy>
    void setPickPolicy(Args&&... args) {
        pick_policy_ = std::make_unique<PickPolicy>(std::forward<Args>(args)...);
    }

    bool hasPickPolicy() const {
        return static_cast<bool>(pick_policy_);
    }

    ServerPtr pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) {
        if (!pick_policy_) {
            return nullptr;
        }

        return pick_policy_->pickServer(request_id, servers).value_or(nullptr);
    }

    void resetServers(const std::vector<ServerPtr>& servers) {
        if (pick_policy_) {
            pick_policy_->resetServers(servers);
        }
    }

    void addServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) {
        if (pick_policy_) {
            pick_policy_->addServerEvent(server, servers);
        }
    }

    void eraseServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) {
        if (pick_policy_) {
            pick_policy_->eraseServerEvent(server, servers);
        }
    }

   private:
    std::unique_ptr<IPickPolicy> pick_policy_;
};

}  // namespace load_balancer
