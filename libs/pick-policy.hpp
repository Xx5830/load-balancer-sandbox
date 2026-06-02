#pragma once

#include "manager.hpp"
namespace load_balancer {
struct IPickPolicy {
    virtual ServerPtr pickServer() = 0;

    virtual ~IPickPolicy() = default;
};


}  // namespace load_balancer