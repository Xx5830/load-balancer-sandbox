#pragma once

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "benchmark-config.hpp"
#include "coroutine-utils.hpp"
#include "generators.hpp"
#include "manager.hpp"
#include "server.hpp"
#include "task.hpp"

namespace load_balancer {

struct ClientStats {
    int requests_sent = 0;
    int successful = 0;
    int failed = 0;
    int retries = 0;
    std::vector<double> latencies;
};

inline asio::awaitable<void> runClient(int group_index,
                                       int client_index,
                                       const ClientGroupConfig& cfg,
                                       ServerManager& manager,
                                       ClientStats& stats,
                                       std::chrono::steady_clock::time_point test_start,
                                       int duration_ms,
                                       int total_request_limit,
                                       int global_seed) {
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);

    std::mt19937 rng(global_seed + group_index * 1000 + client_index);

    int requests_sent = 0;
    auto stop_time = test_start + std::chrono::milliseconds(duration_ms);

    uint64_t base_id = 0;
    if (cfg.sticky_scope == Client) {
        base_id = static_cast<uint64_t>(group_index) * 10000 + client_index;
    } else if (cfg.sticky_scope == Group) {
        base_id = static_cast<uint64_t>(group_index) * 10000;
    }

    int request_counter = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= stop_time) {
            break;
        }
        if (cfg.max_requests > 0 && requests_sent >= cfg.max_requests) {
            break;
        }

        if (!cfg.active_windows.empty()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - test_start).count();
            bool active = false;
            for (auto [start_ms, end_ms] : cfg.active_windows) {
                if (elapsed >= start_ms && elapsed < end_ms) {
                    active = true;
                    break;
                }
            }
            if (!active) {
                timer.expires_after(std::chrono::milliseconds(10));
                co_await timer.async_wait(asio::use_awaitable);
                continue;
            }
        }

        double inter = cfg.inter_arrival_gen->next(rng);
        if (inter > 0.0) {
            timer.expires_after(std::chrono::milliseconds(static_cast<int>(inter)));
            co_await timer.async_wait(asio::use_awaitable);
        }

        double cost = cfg.task_cost_gen->next(rng);

        uint64_t task_id;
        if (cfg.sticky_scope == None) {
            task_id = static_cast<uint64_t>(group_index) * 1000000 + request_counter;
        } else {
            task_id = base_id;
        }

        Task task(task_id, cost);
        bool success = false;
        int retries = 0;
        std::chrono::milliseconds total_latency{0};
        std::string fail_reason;

        auto request_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.retry.timeout_ms);

        while (retries <= cfg.retry.max_retries) {
            auto fut = manager.submitTask(task);
            try {
                auto dur = co_await await_future(manager.submitTask(task));
                total_latency += dur;
                success = true;
                break;
            } catch (const ServerOverloaded&) {
                fail_reason = "server_overloaded";
            } catch (const ServerCrashed&) {
                fail_reason = "server_crashed";
            } catch (...) {
                fail_reason = "unknown";
                break;
            }
            ++retries;
            if (retries <= cfg.retry.max_retries) {
                double delay = cfg.retry.delay_gen->next(rng);
                timer.expires_after(std::chrono::milliseconds(static_cast<int>(delay)));
                co_await timer.async_wait(asio::use_awaitable);
                if (cfg.retry.timeout_ms > 0 && std::chrono::steady_clock::now() > request_deadline) {
                    fail_reason = "timeout";
                    break;
                }
            }
        }

        stats.requests_sent++;
        if (success) {
            stats.successful++;
            stats.latencies.push_back(static_cast<double>(total_latency.count()));
        } else {
            stats.failed++;
        }
        stats.retries += retries;

        ++requests_sent;
        ++request_counter;
    }
}

}  // namespace load_balancer