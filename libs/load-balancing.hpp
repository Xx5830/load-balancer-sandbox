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
        pick_policy_ = std::make_unique<PickPolicy>(args...);
    }
};
}  // namespace load_balancer