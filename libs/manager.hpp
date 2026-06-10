#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "load-balancing.hpp"
#include "pick-policy.hpp"
#include "server.hpp"
#include "task.hpp"

namespace load_balancer {

struct ServerManager {
    explicit ServerManager(std::unique_ptr<LoadBalancer> balancer)
        : balancer_(std::move(balancer)) {
        if (!balancer_) {
            balancer_ = std::make_unique<LoadBalancer>();
        }
        balancer_->resetServers(servers_);
    }

    template <typename PickPolicy, typename... Args>
        requires std::is_base_of_v<IPickPolicy, PickPolicy>
    void setPickPolicy(Args&&... args) {
        std::unique_lock lock(mtx_);
        balancer_->setPickPolicy<PickPolicy>(std::forward<Args>(args)...);
        balancer_->resetServers(servers_);
    }

    uint64_t addServer(uint32_t weight) {
        ServerPtr server = std::make_shared<Server>(weight);

        std::unique_lock lock(mtx_);
        servers_.push_back(server);
        balancer_->addServerEvent(server, servers_);

        return server->getId();
    }

    bool deleteServer(uint64_t id) {
        std::unique_lock lock(mtx_);
        auto it = std::find_if(servers_.begin(), servers_.end(), [id](const ServerPtr& server) { return server->getId() == id; });

        if (it == servers_.end()) {
            return false;
        }

        ServerPtr removed = *it;
        servers_.erase(it);
        balancer_->eraseServerEvent(removed, servers_);
        return true;
    }

    std::vector<uint64_t> listServersIds() const {
        std::shared_lock lock(mtx_);
        std::vector<uint64_t> ids;

        for (const ServerPtr& server : servers_) {
            ids.push_back(server->getId());
        }

        return ids;
    }

    std::vector<ServerPtr> servers() const {
        std::shared_lock lock(mtx_);
        return servers_;
    }

    bool countServer(uint64_t id) const {
        std::shared_lock lock(mtx_);
        return std::find_if(servers_.begin(), servers_.end(), [id](const ServerPtr& server) { return server->getId() == id; }) !=
               servers_.end();
    }

    std::optional<Server::Duration> runTask(const Task& task) {
        ServerPtr server;
        {
            std::shared_lock lock(mtx_);
            if (!balancer_->hasPickPolicy()) {
                return std::nullopt;
            }
            server = balancer_->pickServer(task.getId(), servers_);
        }

        if (!server) {
            return std::nullopt;
        }

        return server->runTask(task);
    }

   private:
    std::vector<ServerPtr> servers_;
    std::unique_ptr<LoadBalancer> balancer_;
    mutable std::shared_mutex mtx_;
};

}  // namespace load_balancer
