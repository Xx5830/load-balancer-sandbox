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

inline bool isPickable(const ServerPtr& server) {
    return server && !server->isCrashed();
}

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

        for (size_t step = 0; step < servers.size(); ++step) {
            ServerPtr candidate = servers[index_];
            index_ = (index_ + 1) % servers.size();
            if (isPickable(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
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

        for (size_t step = 0; step < servers.size(); ++step) {
            if (isPickable(servers[index_])) {
                break;
            }
            index_ = (index_ + 1) % servers.size();
            remaining_for_current_ = 0;
        }
        if (!isPickable(servers[index_])) {
            return std::nullopt;
        }

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

        std::optional<ServerPtr> best;
        uint32_t best_connections = 0;

        for (const ServerPtr& server : servers) {
            if (!isPickable(server)) {
                continue;
            }
            uint32_t current_connections = server->getConnects();
            if (!best || current_connections < best_connections) {
                best = server;
                best_connections = current_connections;
            }
        }

        return best;
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

        const uint64_t hash = mix64(request_id);

        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }
        
        for (size_t step = 0; step < ring_.size(); ++step) {
            if (isPickable(it->second)) {
                return it->second;
            }
            ++it;
            if (it == ring_.end()) {
                it = ring_.begin();
            }
        }
        return std::nullopt;
    }

   private:
    inline static constexpr uint64_t HASH_GOLDEN_RATIO = 0x9e3779b97f4a7c15ULL;
    inline static constexpr uint64_t HASH_MIX_MULTIPLIER_A = 0xbf58476d1ce4e5b9ULL;
    inline static constexpr uint64_t HASH_MIX_MULTIPLIER_B = 0x94d049bb133111ebULL;

    static uint64_t mix64(uint64_t value) {
        value += HASH_GOLDEN_RATIO;
        value = (value ^ (value >> 30)) * HASH_MIX_MULTIPLIER_A;
        value = (value ^ (value >> 27)) * HASH_MIX_MULTIPLIER_B;
        return value ^ (value >> 31);
    }

    uint64_t vnodeHash(const ServerPtr& server, size_t offset) const {
        uint64_t value = server->getId();
        value ^= static_cast<uint64_t>(offset) + HASH_GOLDEN_RATIO + (value << 6) + (value >> 2);
        return mix64(value);
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
