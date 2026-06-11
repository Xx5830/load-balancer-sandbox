#pragma once

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <memory>
#include <random>
#include <vector>

#include "benchmark-config.hpp"
#include "benchmark-stats.hpp"
#include "clients-group.hpp"
#include "manager.hpp"
#include "pick-policy.hpp"
#include "server.hpp"

namespace load_balancer {

class Benchmark {
   public:
    Benchmark(asio::io_context& io, const BenchmarkConfig& cfg)
        : io_(io)
        , config_(cfg) {}

    BenchmarkStats run() {
        auto balancer = std::make_unique<LoadBalancer>();
        switch (config_.algorithm) {
            case Algorithm::RoundRobin:
                balancer->setPickPolicy<RoundRobinPick>();
                break;
            case Algorithm::WeightRoundRobin:
                balancer->setPickPolicy<WeightRoundRobinPick>();
                break;
            case Algorithm::LeastConnections:
                balancer->setPickPolicy<LeastConnectionsPick>();
                break;
            case Algorithm::ConsistentHashing:
                balancer->setPickPolicy<ConsistentHashingPick<>>();
                break;
        }
        ServerManager manager(std::move(balancer), config_.manager_workers);

        for (const auto& srv_cfg : config_.servers) {
            auto server =
                std::make_shared<Server>(srv_cfg.weight, srv_cfg.capacity, srv_cfg.max_parallel_requests, config_.server_model_params);
            manager.addServer(server);
        }

        std::vector<std::vector<ClientStats>> all_stats;
        all_stats.resize(config_.client_groups.size());
        for (size_t g = 0; g < config_.client_groups.size(); ++g) {
            all_stats[g].resize(config_.client_groups[g].count);
        }

        auto test_start = std::chrono::steady_clock::now();
        for (size_t g = 0; g < config_.client_groups.size(); ++g) {
            const auto& group_cfg = config_.client_groups[g];
            for (int i = 0; i < group_cfg.count; ++i) {
                asio::co_spawn(io_,
                               runClient(static_cast<int>(g),
                                         i,
                                         group_cfg,
                                         manager,
                                         all_stats[g][i],
                                         test_start,
                                         config_.duration_ms,
                                         config_.total_request_limit,
                                         config_.seed),
                               asio::detached);
            }
        }

        asio::steady_timer end_timer(io_);
        end_timer.expires_after(std::chrono::milliseconds(config_.duration_ms));
        end_timer.async_wait([this](std::error_code) { io_.stop(); });

        io_.run();

        auto test_end = std::chrono::steady_clock::now();
        double actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start).count();

        BenchmarkStats stats;
        stats.actual_duration_ms = actual_duration;

        for (auto& group_stats : all_stats) {
            for (auto& st : group_stats) {
                stats.total_requests += st.requests_sent;
                stats.successful += st.successful;
                stats.failed += st.failed;
                stats.retries += st.retries;
                stats.latencies.insert(stats.latencies.end(), st.latencies.begin(), st.latencies.end());
            }
        }

        std::sort(stats.latencies.begin(), stats.latencies.end());
        if (!stats.latencies.empty()) {
            size_t n = stats.latencies.size();
            stats.min_latency_ms = stats.latencies.front();
            stats.max_latency_ms = stats.latencies.back();
            double sum = 0.0;
            for (double v : stats.latencies) sum += v;
            stats.avg_latency_ms = sum / n;
            stats.p50_latency_ms = stats.latencies[static_cast<size_t>(n * 0.5)];
            stats.p95_latency_ms = stats.latencies[static_cast<size_t>(n * 0.95)];
            stats.p99_latency_ms = stats.latencies[static_cast<size_t>(n * 0.99)];
        }

        if (actual_duration > 0.0) {
            stats.throughput_rps = stats.successful / (actual_duration / 1000.0);
        }

        return stats;
    }

   private:
    asio::io_context& io_;
    BenchmarkConfig config_;
};

}  // namespace load_balancer