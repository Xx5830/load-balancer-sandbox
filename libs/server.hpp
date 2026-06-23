#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "task.hpp"

namespace load_balancer {

struct ServerCrashed : std::runtime_error {
    ServerCrashed()
        : std::runtime_error("server crashed") {}
};

struct ServerOverloaded : std::runtime_error {
    ServerOverloaded()
        : std::runtime_error("server overload") {}
};

struct ServerModelParams {
    double load_recovery_rate_ = 0.25;
    double load_slowdown_factor_ = 0.9;
    double task_load_factor_ = 0.35;
    double connection_load_factor_ = 0.04;
    double min_task_seconds_ = 0.001;
    double reject_threshold_seconds_ = 5.0;
    double overload_reject_factor_ = 1.0;
    double min_weight_factor_ = 0.08;
};

struct ServerStats {
    uint64_t id_ = 0;
    uint32_t weight_ = 0;
    double capacity_ = 1.0;
    uint32_t max_parallel_requests_ = 1;
    uint64_t requests_received_ = 0;
    uint64_t successful_ = 0;
    uint64_t failed_ = 0;
    double total_time_processing_ms_ = 0.0;
    double avg_time_ms_ = 0.0;
    double min_time_ms_ = 0.0;
    double max_time_ms_ = 0.0;
    double avg_load_ = 0.0;
    double peak_load_ = 0.0;
    uint64_t crashes_ = 0;
};

class Server {
   public:
    using Clock = std::chrono::steady_clock;
    using BackgroundLoadSource = std::function<double()>;

   private:
    uint64_t id_;
    uint32_t weight_;
    double capacity_;
    uint32_t max_parallel_requests_;
    ServerModelParams params_;
    BackgroundLoadSource background_load_source_;

    std::queue<TaskItem> queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::atomic<bool> crashed_{false};
    std::atomic<bool> started_{false};

    std::vector<std::thread> workers_;

    std::atomic<uint32_t> cnt_connects_{0};
    std::atomic<uint32_t> active_requests_{0};
    double background_load_value_ = 0.0;

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_{0};
    std::atomic<uint64_t> failed_{0};
    std::atomic<uint64_t> total_time_ms_{0};
    std::atomic<uint64_t> min_time_ms_{UINT64_MAX};
    std::atomic<uint64_t> max_time_ms_{0};
    mutable std::mutex load_stats_mutex_;
    double load_sum_ = 0.0;
    uint64_t load_count_ = 0;
    double peak_load_ = 0.0;
    std::atomic<uint64_t> crashes_{0};

    inline static std::atomic<uint64_t> next_id_{0};

    mutable std::mutex state_mutex_;
    mutable double load_ = 0.0;
    mutable Clock::time_point last_update_;

    Duration estimateDurationLocked(const Task& task) const {
        double effective_load = load_ + background_load_value_;
        if (effective_load > 1.0)
            effective_load = 1.0;
        double available_factor = std::max(params_.min_weight_factor_, 1.0 - effective_load * params_.load_slowdown_factor_);
        double effective_power = static_cast<double>(weight_) * capacity_ * available_factor;
        double seconds = static_cast<double>(task.getCost()) / std::max(1e-9, effective_power);
        if (seconds < params_.min_task_seconds_)
            seconds = params_.min_task_seconds_;
        return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(seconds));
    }

    double loadAfterTaskStartLocked(const Task& task, uint32_t active_requests) const {
        double task_pressure =
            params_.task_load_factor_ * static_cast<double>(task.getCost()) / std::max(1.0, static_cast<double>(weight_));
        double parallelism = std::max(1.0, static_cast<double>(max_parallel_requests_));
        double active_ratio = std::min(1.0, static_cast<double>(active_requests) / parallelism);
        double connection_pressure = params_.connection_load_factor_ * active_ratio;
        return std::min(1.0, load_ + task_pressure + connection_pressure);
    }

    void refreshLoadLocked(Clock::time_point now) const {
        if (now <= last_update_)
            return;
        auto elapsed = std::chrono::duration<double>(now - last_update_).count();
        double recovery = elapsed * static_cast<double>(weight_) * params_.load_recovery_rate_;
        load_ = std::max(0.0, load_ - recovery);
        last_update_ = now;
    }

    void updateBackgroundLoad() {
        if (background_load_source_) {
            background_load_value_ = std::clamp(background_load_source_(), 0.0, 1.0);
        } else {
            background_load_value_ = 0.0;
        }
    }

    void workerLoop() {
        while (true) {
            Task task;
            std::promise<Duration> prom;
            {
                std::unique_lock<std::mutex> lk(queue_mutex_);
                cv_.wait(lk, [this]() { return stop_ || !queue_.empty(); });

                if (stop_) {
                    while (!queue_.empty()) {
                        auto p = std::move(queue_.front().promise);
                        queue_.pop();
                        failed_.fetch_add(1);
                        cnt_connects_.fetch_sub(1);
                        p.set_exception(std::make_exception_ptr(ServerCrashed{}));
                    }
                    return;
                }
                task = std::move(queue_.front().task);
                prom = std::move(queue_.front().promise);
                queue_.pop();
            }

            Duration planned;
            bool reject = false;
            {
                std::lock_guard lk(state_mutex_);
                updateBackgroundLoad();
                refreshLoadLocked(Clock::now());
                planned = estimateDurationLocked(task);

                if (params_.reject_threshold_seconds_ > 0.0) {
                    double threshold_ms = params_.reject_threshold_seconds_ * 1000.0 * (1.0 - load_ * params_.overload_reject_factor_);
                    if (threshold_ms < 0.0)
                        threshold_ms = 0.0;
                    if (planned.count() > threshold_ms)
                        reject = true;
                }

                if (!reject) {
                    uint32_t active_requests = active_requests_.fetch_add(1, std::memory_order_relaxed) + 1;
                    load_ = loadAfterTaskStartLocked(task, active_requests);
                }
            }

            if (reject) {
                failed_.fetch_add(1);
                cnt_connects_.fetch_sub(1);
                prom.set_exception(std::make_exception_ptr(ServerOverloaded{}));
                continue;
            }

            auto start = Clock::now();
            std::this_thread::sleep_for(planned);
            auto end = Clock::now();
            auto actual_duration = std::chrono::duration_cast<Duration>(end - start);
            active_requests_.fetch_sub(1, std::memory_order_relaxed);

            double load_snapshot = 0.0;
            {
                std::lock_guard<std::mutex> lk(state_mutex_);
                refreshLoadLocked(end);
                load_snapshot = load_;
            }

            successful_.fetch_add(1);
            auto ms = static_cast<uint64_t>(actual_duration.count());
            total_time_ms_.fetch_add(ms);

            uint64_t old_min = min_time_ms_.load();
            while (ms < old_min && !min_time_ms_.compare_exchange_weak(old_min, ms)) {
            }
            uint64_t old_max = max_time_ms_.load();
            while (ms > old_max && !max_time_ms_.compare_exchange_weak(old_max, ms)) {
            }

            {
                std::lock_guard<std::mutex> lk(load_stats_mutex_);
                load_sum_ += load_snapshot;
                load_count_++;
                if (load_snapshot > peak_load_)
                    peak_load_ = load_snapshot;
            }

            cnt_connects_.fetch_sub(1);

            prom.set_value(actual_duration);
        }
    }

   public:
    Server(uint32_t weight,
           double capacity,
           uint32_t max_parallel_requests,
           ServerModelParams params = {},
           BackgroundLoadSource background_load_source = {})
        : id_(next_id_.fetch_add(1, std::memory_order_relaxed))
        , weight_(weight)
        , capacity_(capacity)
        , max_parallel_requests_(max_parallel_requests)
        , params_(std::move(params))
        , background_load_source_(std::move(background_load_source)) {
        if (weight_ == 0)
            throw std::invalid_argument("Server weight must be > 0");
        last_update_ = Clock::now();
        for (uint32_t i = 0; i < max_parallel_requests_; ++i) {
            workers_.emplace_back(&Server::workerLoop, this);
        }
    }

    std::future<Duration> submit(Task task) {
        cnt_connects_.fetch_add(1);
        total_requests_.fetch_add(1);
        std::promise<Duration> res;
        auto fut = res.get_future();

        {
            std::lock_guard lk(queue_mutex_);
            if (stop_) {
                failed_.fetch_add(1);
                cnt_connects_.fetch_sub(1);
                res.set_exception(std::make_exception_ptr(ServerCrashed{}));
                return fut;
            }
            queue_.push({std::move(task), std::move(res)});
        }
        cv_.notify_one();
        return fut;
    }

    void markStarted() {
        started_.store(true);
    }
    bool isStarted() const {
        return started_.load();
    }
    bool isCrashed() const {
        return crashed_.load();
    }

    void crash() {
        if (!crashed_.exchange(true)) {
            crashes_.fetch_add(1);
            {
                std::lock_guard lk(queue_mutex_);
                stop_ = true;
            }
            cv_.notify_all();
        }
    }

    uint64_t getId() const {
        return id_;
    }
    uint32_t getWeight() const {
        return weight_;
    }
    uint32_t getConnects() const {
        return cnt_connects_.load();
    }

    double getCurrentLoad() const {
        std::lock_guard lk(state_mutex_);
        refreshLoadLocked(Clock::now());
        return load_;
    }

    ServerStats getStats() const {
        ServerStats s;
        s.id_ = id_;
        s.weight_ = weight_;
        s.capacity_ = capacity_;
        s.max_parallel_requests_ = max_parallel_requests_;
        s.requests_received_ = total_requests_.load();
        s.successful_ = successful_.load();
        s.failed_ = failed_.load();
        s.total_time_processing_ms_ = static_cast<double>(total_time_ms_.load());

        uint64_t succ = successful_.load();
        if (succ > 0) {
            s.avg_time_ms_ = static_cast<double>(total_time_ms_.load()) / succ;
            s.min_time_ms_ = static_cast<double>(min_time_ms_.load());
            s.max_time_ms_ = static_cast<double>(max_time_ms_.load());
        } else {
            s.min_time_ms_ = 0.0;
            s.max_time_ms_ = 0.0;
        }

        {
            std::lock_guard<std::mutex> lk(load_stats_mutex_);
            if (load_count_ > 0)
                s.avg_load_ = load_sum_ / load_count_;
            s.peak_load_ = peak_load_;
        }
        s.crashes_ = crashes_.load();
        return s;
    }

    ~Server() {
        crash();
        for (auto& w : workers_) {
            if (w.joinable())
                w.join();
        }
    }
};

using ServerPtr = std::shared_ptr<Server>;

}  // namespace load_balancer