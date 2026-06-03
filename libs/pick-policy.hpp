#pragma once

#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include "server.hpp"

namespace load_balancer {
struct IPickPolicy {
   protected:
    std::vector<ServerPtr>* servers_ = nullptr;

   public:
    virtual std::optional<ServerPtr> pickServer(uint64_t id) = 0;
    IPickPolicy() {}

    virtual void setServers(std::vector<ServerPtr>* servers) {
        servers_ = servers;
    }

    virtual void addServerEvent(ServerPtr server) {}
    virtual void eraseServerEvent(ServerPtr server) {}
    virtual ~IPickPolicy() {};
};

struct RoundRobinPick : IPickPolicy {
   protected:
    size_t index_;

   public:
    RoundRobinPick()
        : index_(0) {}

    std::optional<ServerPtr> pickServer(uint64_t id) {
        if (!servers_ || servers_->empty()) {
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
    WeightRoundRobinPick()
        : index_(-1)
        , cnt_(0) {}

    std::optional<ServerPtr> pickServer(uint64_t id) {
        if (!servers_ || servers_->empty()) {
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
        --cnt_;

        return (*servers_)[index_];
    }
};

struct LeastConnectionsPick : IPickPolicy {
   protected:
   public:
    LeastConnectionsPick() {}

    std::optional<ServerPtr> pickServer(uint64_t id) {
        if (!servers_ || servers_->empty()) {
            return std::nullopt;
        }

        size_t index = 0, best = 0;
        size_t best_value = std::numeric_limits<size_t>::max();

        for (size_t index = 0; index < servers_->size(); index++) {
            if (size_t current = (*servers_)[index]->getConnects();
                current < best_value) {
                best = index;
                best_value = current;
            }
        }

        return (*servers_)[best];
    }
};

template <uint32_t VnodesPerWeight = 15>
struct ConsistentHashingPick : IPickPolicy {
   protected:
    std::map<uint64_t, ServerPtr> ring_;

    uint64_t vnodeHash(const ServerPtr& server, size_t offset) const {
        uint64_t h1 = std::hash<uint64_t>{}(server->getId());
        uint64_t h2 = std::hash<size_t>{}(offset);
        return h1 ^ (h2 << 32);
    }

    void rebuildRing() {
        ring_.clear();
        if (!servers_ || servers_->empty())
            return;
        for (const auto& srv : *servers_) {
            int weight = srv->getWeight();
            int total_vnodes = weight * VnodesPerWeight;
            for (int i = 0; i < total_vnodes; ++i) {
                ring_.emplace(vnodeHash(srv, i), srv);
            }
        }
    }

   public:
    ConsistentHashingPick() = default;

    void setServers(std::vector<ServerPtr>* servers) override {
        servers_ = servers;
        rebuildRing();
    }

    void addServerEvent(ServerPtr server) override {
        if (!servers_) {
            return;
        }
        int weight = server->getWeight();
        int total_vnodes = weight * VnodesPerWeight;
        for (int i = 0; i < total_vnodes; ++i) {
            ring_.emplace(vnodeHash(server, i), server);
        }
    }

    void eraseServerEvent(ServerPtr server) override {
        int weight = server->getWeight();
        int total_vnodes = weight * VnodesPerWeight;
        for (int i = 0; i < total_vnodes; ++i) {
            ring_.erase(vnodeHash(server, i));
        }
    }

    std::optional<ServerPtr> pickServer(uint64_t id) override {
        if (ring_.empty())
            return std::nullopt;

        uint64_t hash = std::hash<uint64_t>{}(id);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }
        return it->second;
    }
};

}  // namespace load_balancer