#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "server.hpp"

namespace load_balancer {

enum class Algorithm { RoundRobin, WeightRoundRobin, LeastConnections, ConsistentHashing };

struct IPickPolicy {
    virtual std::optional<ServerPtr> pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) = 0;

    virtual void resetServers(const std::vector<ServerPtr>& servers) {}

    virtual void addServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) {}

    virtual void eraseServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) {}

    virtual ~IPickPolicy() = default;
};

struct RoundRobinPick : IPickPolicy {
   public:
    std::optional<ServerPtr> pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);

        if (servers.empty()) {
            return std::nullopt;
        }

        if (index_ >= servers.size()) {
            index_ = 0;
        }

        ServerPtr selected = servers[index_];
        index_ = (index_ + 1) % servers.size();
        return selected;
    }

    void resetServers(const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);
        index_ = servers.empty() ? 0 : index_ % servers.size();
    }

    void eraseServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) override {
        resetServers(servers);
    }

   private:
    size_t index_{0};
    mutable std::mutex mtx_;
};

struct WeightRoundRobinPick : IPickPolicy {
   public:
    std::optional<ServerPtr> pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);

        if (servers.empty()) {
            index_ = 0;
            remaining_for_current_ = 0;
            return std::nullopt;
        }

        normalizeLocked(servers);

        if (remaining_for_current_ == 0) {
            remaining_for_current_ = servers[index_]->getWeight();
        }

        ServerPtr selected = servers[index_];
        --remaining_for_current_;
        if (remaining_for_current_ == 0) {
            index_ = (index_ + 1) % servers.size();
        }

        return selected;
    }

    void resetServers(const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);
        normalizeLocked(servers);
        remaining_for_current_ = 0;
    }

    void eraseServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) override {
        resetServers(servers);
    }

   private:
    void normalizeLocked(const std::vector<ServerPtr>& servers) {
        if (servers.empty()) {
            index_ = 0;
            remaining_for_current_ = 0;
            return;
        }

        if (index_ >= servers.size()) {
            index_ %= servers.size();
            remaining_for_current_ = 0;
        }
    }

    size_t index_{0};
    uint32_t remaining_for_current_{0};
    mutable std::mutex mtx_;
};

struct LeastConnectionsPick : IPickPolicy {
    std::optional<ServerPtr> pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) override {
        if (servers.empty()) {
            return std::nullopt;
        }

        size_t best = 0;
        uint32_t best_connections = servers[0]->getConnects();

        for (size_t index = 1; index < servers.size(); ++index) {
            uint32_t current_connections = servers[index]->getConnects();

            if (current_connections < best_connections) {
                best = index;
                best_connections = current_connections;
            }
        }

        return servers[best];
    }
};

template <uint32_t VnodesPerWeight = 15>
struct ConsistentHashingPick : IPickPolicy {
   public:
    void resetServers(const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);
        ring_.clear();
        for (const ServerPtr& server : servers) {
            addServerLocked(server);
        }
    }

    void addServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);
        addServerLocked(server);
    }

    void eraseServerEvent(const ServerPtr& server, const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);
        eraseServerLocked(server);
    }

    std::optional<ServerPtr> pickServer(uint64_t request_id, const std::vector<ServerPtr>& servers) override {
        std::lock_guard lock(mtx_);

        if (servers.empty() || ring_.empty()) {
            return std::nullopt;
        }

        const uint64_t hash = std::hash<uint64_t>{}(request_id);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }

        return it->second;
    }

   private:
    uint64_t vnodeHash(const ServerPtr& server, size_t offset) const {
        // TODO: выбрать хэш-функцию сильнее
        uint64_t h1 = std::hash<uint64_t>{}(server->getId());
        uint64_t h2 = std::hash<size_t>{}(offset);
        return h1 ^ (h2 << 32);
    }

    void addServerLocked(const ServerPtr& server) {
        uint32_t total_vnodes = server->getWeight() * VnodesPerWeight;
        for (uint32_t i = 0; i < total_vnodes; ++i) {
            ring_.emplace(vnodeHash(server, i), server);
        }
    }

    void eraseServerLocked(const ServerPtr& server) {
        uint32_t total_vnodes = server->getWeight() * VnodesPerWeight;
        for (uint32_t i = 0; i < total_vnodes; ++i) {
            ring_.erase(vnodeHash(server, i));
        }
    }

    std::map<uint64_t, ServerPtr> ring_;
    mutable std::mutex mtx_;
};

}  // namespace load_balancer
