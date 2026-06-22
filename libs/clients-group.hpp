#pragma once

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
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
    int server_crashed_failures = 0;
    int server_overloaded_failures = 0;
    int timeout_failures = 0;
    int unknown_failures = 0;
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
                                       int global_seed,
                                       std::shared_ptr<std::atomic<int>> shared_request_counter = nullptr) {
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

        if (std::chrono::steady_clock::now() >= stop_time) {
            break;
        }

        if (total_request_limit > 0) {
            if (shared_request_counter) {
                int current = shared_request_counter->load();
                bool reserved = false;
                while (current < total_request_limit) {
                    if (shared_request_counter->compare_exchange_weak(current, current + 1)) {
                        reserved = true;
                        break;
                    }
                }
                if (!reserved) {
                    break;
                }
            } else if (requests_sent >= total_request_limit) {
                break;
            }
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

        for (int attempt = 0; attempt <= cfg.retry.max_retries; ++attempt) {
            if (attempt > 0) {
                ++retries;
            }
            auto fut = manager.submitTask(task);
            try {
                Duration dur;
                if (cfg.retry.timeout_ms > 0) {
                    auto maybe_duration = co_await await_future_until(std::move(fut), request_deadline);
                    if (!maybe_duration) {
                        fail_reason = "timeout";
                        break;
                    }
                    dur = *maybe_duration;
                } else {
                    dur = co_await await_future(std::move(fut));
                }
                total_latency += dur;
                success = true;
                break;
            } catch (const ServerOverloaded&) {
                fail_reason = "server_overloaded";
            } catch (const ServerCrashed&) {
                fail_reason = "server_crashed";
            } catch (const NoServerAvailable&) {
                fail_reason = "unknown";
            } catch (const NoPolicy&) {
                fail_reason = "unknown";
            } catch (...) {
                fail_reason = "unknown";
                break;
            }
            if (attempt == cfg.retry.max_retries) {
                break;
            }
            if (cfg.retry.timeout_ms > 0 && std::chrono::steady_clock::now() >= request_deadline) {
                fail_reason = "timeout";
                break;
            }

            double delay = cfg.retry.delay_gen->next(rng);
            if (delay > 0.0) {
                auto retry_delay = std::chrono::milliseconds(static_cast<int>(delay));
                if (cfg.retry.timeout_ms > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (now + retry_delay >= request_deadline) {
                        timer.expires_at(request_deadline);
                        co_await timer.async_wait(asio::use_awaitable);
                        fail_reason = "timeout";
                        break;
                    }
                }
                timer.expires_after(retry_delay);
                co_await timer.async_wait(asio::use_awaitable);
            }
        }

        stats.requests_sent++;
        if (success) {
            stats.successful++;
            stats.latencies.push_back(static_cast<double>(total_latency.count()));
        } else {
            stats.failed++;
            if (fail_reason == "server_overloaded") {
                stats.server_overloaded_failures++;
            } else if (fail_reason == "timeout") {
                stats.timeout_failures++;
            } else if (fail_reason == "unknown") {
                stats.unknown_failures++;
            } else {
                stats.server_crashed_failures++;
            }
        }
        stats.retries += retries;

        ++requests_sent;
        ++request_counter;
    }
}

}  // namespace load_balancer
