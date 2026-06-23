#pragma once

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <system_error>
#include <utility>
#include <vector>

#include "asio/impl/co_spawn.hpp"
#include "benchmark-config.hpp"
#include "benchmark-stats.hpp"
#include "clients-group.hpp"
#include "generators.hpp"
#include "load-balancing.hpp"
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
        auto manager = std::make_shared<ServerManager>(std::move(balancer), config_.manager_workers);

        std::vector<ServerPtr> all_servers;

        for (size_t server_index = 0; server_index < config_.servers.size(); ++server_index) {
            const auto& srv_cfg = config_.servers[server_index];
            auto server = std::make_shared<Server>(srv_cfg.weight,
                                                   srv_cfg.capacity,
                                                   srv_cfg.max_parallel_requests,
                                                   config_.server_model_params,
                                                   makeBackgroundLoadSource(srv_cfg.background_load_gen, server_index));
            all_servers.push_back(server);

            if (srv_cfg.start_at_ms <= 0) {
                server->markStarted();
                manager->addServer(server);
            } else {
                asio::co_spawn(io_, scheduleServerStart(manager, server, srv_cfg.start_at_ms), asio::detached);
            }

            if (srv_cfg.crash_at_ms > 0) {
                asio::co_spawn(io_, scheduleServerCrash(server, srv_cfg.crash_at_ms), asio::detached);
            }
        }

        std::vector<std::vector<std::shared_ptr<ClientStats>>> all_stats;
        all_stats.resize(config_.client_groups.size());
        for (size_t g = 0; g < config_.client_groups.size(); ++g) {
            all_stats[g].resize(config_.client_groups[g].count);
            for (int i = 0; i < config_.client_groups[g].count; ++i) {
                all_stats[g][i] = std::make_shared<ClientStats>();
            }
        }

        auto shared_request_counter = std::make_shared<std::atomic<int>>(0);
        auto test_start = std::chrono::steady_clock::now();

        auto timeline_mtx = std::make_shared<std::mutex>();
        auto timeline_latencies = std::make_shared<std::vector<double>>();

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
                                         config_.seed,
                                         shared_request_counter,
                                         timeline_mtx,
                                         timeline_latencies),
                               asio::detached);
            }
        }

        std::vector<TimelineEntry> timeline_result;
        asio::co_spawn(io_, timelineMonitor(all_servers, timeline_result, timeline_mtx, timeline_latencies), asio::detached);

        asio::steady_timer end_timer(io_);
        end_timer.expires_after(std::chrono::milliseconds(config_.duration_ms));
        end_timer.async_wait([this](std::error_code) { io_.stop(); });

        io_.run();

        auto test_end = std::chrono::steady_clock::now();
        double actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start).count();

        BenchmarkStats stats;
        stats.actual_duration_ms = actual_duration;
        stats.timeline = std::move(timeline_result);

        for (size_t i = 0; i < all_servers.size(); ++i) {
            ServerResult sr;
            auto s_st = all_servers[i]->getStats();
            sr.id = s_st.id_;
            sr.weight = s_st.weight_;
            sr.capacity = s_st.capacity_;
            sr.max_parallel_requests = s_st.max_parallel_requests_;
            sr.start_at_ms = config_.servers[i].start_at_ms;
            sr.crash_at_ms = config_.servers[i].crash_at_ms;
            sr.requests_received = s_st.requests_received_;
            sr.successful = s_st.successful_;
            sr.failed = s_st.failed_;
            sr.total_time_processing_ms = s_st.total_time_processing_ms_;
            sr.avg_time_ms = s_st.avg_time_ms_;
            sr.min_time_ms = s_st.min_time_ms_;
            sr.max_time_ms = s_st.max_time_ms_;
            sr.avg_load = s_st.avg_load_;
            sr.peak_load = s_st.peak_load_;
            sr.crashes = s_st.crashes_;
            stats.servers.push_back(sr);
        }

        for (size_t g = 0; g < config_.client_groups.size(); ++g) {
            ClientGroupResult cg_res;
            cg_res.group_index = g;
            auto scope = config_.client_groups[g].sticky_scope;
            cg_res.sticky_scope = (scope == StickyScope::None ? "none" : (scope == StickyScope::Client ? "client" : "group"));
            cg_res.num_clients = config_.client_groups[g].count;

            std::vector<double> grp_latencies;
            for (auto& st_ptr : all_stats[g]) {
                auto& st = *st_ptr;
                stats.total_requests += st.requests_sent;
                stats.successful += st.successful;
                stats.failed += st.failed;
                stats.retries += st.retries;
                stats.server_crashed_failures += st.server_crashed_failures;
                stats.server_overloaded_failures += st.server_overloaded_failures;
                stats.timeout_failures += st.timeout_failures;
                stats.unknown_failures += st.unknown_failures;
                stats.latencies.insert(stats.latencies.end(), st.latencies.begin(), st.latencies.end());

                cg_res.requests_sent += st.requests_sent;
                cg_res.successful += st.successful;
                cg_res.failed_final += st.failed;
                cg_res.retries += st.retries;
                grp_latencies.insert(grp_latencies.end(), st.latencies.begin(), st.latencies.end());
            }

            if (!grp_latencies.empty()) {
                std::sort(grp_latencies.begin(), grp_latencies.end());
                double sum = 0.0;
                for (double v : grp_latencies) sum += v;
                cg_res.latency_mean = sum / grp_latencies.size();
                cg_res.latency_max = grp_latencies.back();
                cg_res.latency_p95 = grp_latencies[static_cast<size_t>(grp_latencies.size() * 0.95)];
            }
            stats.client_groups.push_back(cg_res);
        }

        std::sort(stats.latencies.begin(), stats.latencies.end());
        if (!stats.latencies.empty()) {
            size_t n = stats.latencies.size();
            stats.min_latency_ms = stats.latencies.front();
            stats.max_latency_ms = stats.latencies.back();
            double sum = 0.0;
            for (double v : stats.latencies) sum += v;
            stats.avg_latency_ms = sum / n;
            stats.p50_latency_ms = stats.latencies[static_cast<size_t>(n * 0.50)];
            stats.p95_latency_ms = stats.latencies[static_cast<size_t>(n * 0.95)];
            stats.p99_latency_ms = stats.latencies[static_cast<size_t>(n * 0.99)];

            double sq_sum = 0.0;
            for (double v : stats.latencies) {
                double diff = v - stats.avg_latency_ms;
                sq_sum += diff * diff;
            }
            stats.stddev_latency_ms = std::sqrt(sq_sum / n);
        }

        if (actual_duration > 0.0) {
            stats.throughput_rps = stats.successful / (actual_duration / 1000.0);
        }

        return stats;
    }

   private:
    asio::awaitable<void> scheduleServerStart(std::shared_ptr<ServerManager> manager, ServerPtr server, int delay_ms) {
        auto executor = co_await asio::this_coro::executor;
        asio::steady_timer timer(executor, std::chrono::milliseconds(delay_ms));
        co_await timer.async_wait(asio::use_awaitable);
        server->markStarted();
        manager->addServer(server);
    }

    asio::awaitable<void> scheduleServerCrash(ServerPtr server, int delay_ms) {
        auto executor = co_await asio::this_coro::executor;
        asio::steady_timer timer(executor, std::chrono::milliseconds(delay_ms));
        co_await timer.async_wait(asio::use_awaitable);
        server->crash();
    }

    asio::awaitable<void> timelineMonitor(std::vector<ServerPtr> all_servers,
                                          std::vector<TimelineEntry>& timeline,
                                          std::shared_ptr<std::mutex> timeline_mtx,
                                          std::shared_ptr<std::vector<double>> timeline_latencies) {
        auto executor = co_await asio::this_coro::executor;
        asio::steady_timer timer(executor);
        int elapsed_ms = 0;

        uint64_t last_global_req = 0;
        uint64_t last_global_succ = 0;
        uint64_t last_global_fail = 0;

        while (elapsed_ms < config_.duration_ms) {
            timer.expires_after(std::chrono::milliseconds(1000));
            co_await timer.async_wait(asio::use_awaitable);
            elapsed_ms += 1000;

            TimelineEntry entry;
            entry.t_ms = elapsed_ms;

            uint64_t cur_req = 0, cur_succ = 0, cur_fail = 0;
            double load_sum = 0.0;
            int active_srv_count = 0;

            for (const auto& srv : all_servers) {
                auto st = srv->getStats();
                cur_req += st.requests_received_;
                cur_succ += st.successful_;
                cur_fail += st.failed_;

                if (srv->isStarted() && !srv->isCrashed()) {
                    entry.active_connections += srv->getConnects();
                    load_sum += srv->getCurrentLoad();
                    active_srv_count++;
                }
            }

            entry.new_requests = cur_req - last_global_req;
            entry.successful = cur_succ - last_global_succ;
            entry.failed_this_second = cur_fail - last_global_fail;
            entry.requests_in_flight = entry.active_connections;
            entry.total_load_avg = (active_srv_count > 0) ? (load_sum / active_srv_count) : 0.0;

            std::vector<double> sec_latencies;
            {
                std::lock_guard<std::mutex> lk(*timeline_mtx);
                sec_latencies.swap(*timeline_latencies);
            }

            if (!sec_latencies.empty()) {
                std::sort(sec_latencies.begin(), sec_latencies.end());
                double sum = 0.0;
                for (double l : sec_latencies) sum += l;
                entry.avg_latency_ms = sum / sec_latencies.size();
                entry.p95_latency_ms = sec_latencies[static_cast<size_t>(sec_latencies.size() * 0.95)];
            } else {
                entry.avg_latency_ms = 0.0;
                entry.p95_latency_ms = 0.0;
            }

            last_global_req = cur_req;
            last_global_succ = cur_succ;
            last_global_fail = cur_fail;

            timeline.push_back(entry);
        }
    }

    Server::BackgroundLoadSource makeBackgroundLoadSource(const GeneratorPtr& generator, size_t server_index) const {
        if (!generator)
            return {};
        auto rng = std::make_shared<std::mt19937>(config_.seed + static_cast<int>(100000 + server_index));
        return [generator, rng]() mutable { return generator->next(*rng); };
    }

    asio::io_context& io_;
    BenchmarkConfig config_;
};

}  // namespace load_balancer