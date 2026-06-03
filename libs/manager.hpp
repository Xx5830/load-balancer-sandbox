#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "load-balancing.hpp"
#include "server.hpp"

namespace load_balancer {

struct ServerManager {
   private:
    std::vector<ServerPtr> servers_;
    std::unique_ptr<LoadBalancer> balancer_;
    mutable std::shared_mutex mtx_;

   public:
    explicit ServerManager(std::unique_ptr<LoadBalancer> balancer)
        : balancer_(std::move(balancer)) {
        if (balancer_) {
            balancer_->setServers(&servers_);
        }
    }

    uint64_t addServer(int weight) {
        ServerPtr server = std::make_shared<Server>(weight);

        {
            std::unique_lock lock(mtx_);
            servers_.push_back(server);
        }

        balancer_->addServerEvent(server);

        return server->getId();
    }

    bool deleteServer(uint64_t id) noexcept {
        std::unique_lock lock(mtx_);
        if (auto it = std::find_if(
                servers_.begin(),
                servers_.end(),
                [&id](const ServerPtr& elem) { return elem->getId() == id; });
            it != servers_.end()) {
            balancer_->eraseServerEvent(*it);
            servers_.erase(it);
            return true;
        }

        return false;
    }

    std::vector<uint64_t> listServers() const noexcept {
        std::shared_lock lock(mtx_);
        std::vector<uint64_t> ids;
        for (const auto& now : servers_) {
            ids.push_back(now->getId());
        }

        return ids;
    }

    std::vector<ServerPtr>& servers() noexcept {
        return servers_;
    }

    bool countServer(uint64_t id) const noexcept {
        return std::find_if(servers_.begin(),
                            servers_.end(),
                            [&id](const ServerPtr& elem) {
                                return elem->getId() == id;
                            }) != servers_.end();
    }

    Server::Duration runTask(Task task) {
        ServerPtr server;
        {
            std::shared_lock lock(mtx_);
            server = balancer_->pickServer(task.getId());
        }

        if (!server) {
            // переделать на optional
            throw std::runtime_error("Нет Серверов");
        }
        return server->runTask(task);
    }
};

}  // namespace load_balancer