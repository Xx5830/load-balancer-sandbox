#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "load-balancing.hpp"
#include "server.hpp"

namespace load_balancer {

struct ServerManager {
   private:
    std::vector<ServerPtr> servers_;
    std::unique_ptr<LoadBalancer> balancer_;

   public:
    ServerManager(std::unique_ptr<LoadBalancer> balancer)
        : balancer_(std::move(balancer)) {
        if (balancer_) {
            balancer_->setServers(&servers_);
        }
    }

    uint64_t addServer(int weight) {
        ServerPtr server = std::make_shared<Server>(weight);

        servers_.push_back(server);
        balancer_->addServerEvent(server);

        return server->getId();
    }

    std::vector<uint64_t> listServers() const noexcept {
        std::vector<uint64_t> ids;
        for (const auto& now : servers_) {
            ids.push_back(now->getId());
        }

        return ids;
    }

    std::vector<ServerPtr>& servers() noexcept {
        return servers_;
    }

    bool deleteServer(uint64_t id) noexcept {
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

    bool countServer(uint64_t id) const noexcept {
        return std::find_if(servers_.begin(),
                            servers_.end(),
                            [&id](const ServerPtr& elem) {
                                return elem->getId() == id;
                            }) != servers_.end();
    }

    Server::Duration runTask(Task task) {
        ServerPtr server = balancer_->pickServer(task.getId());
        if (!server) {
            //переделать на optional
            throw std::runtime_error("Нет Серверов");
        }
        return server->runTask(task);
    }
};

}  // namespace load_balancer