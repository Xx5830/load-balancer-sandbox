#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "task.hpp"

namespace load_balancer {

//TODO: коэффициенты попросил подобрать нейронку, нужно тестить и менять на свои скорее всего
inline constexpr double MIN_LOAD = 0.0;
inline constexpr double MAX_LOAD = 1.0;
inline constexpr double LOAD_RECOVERY_RATE = 0.25;
inline constexpr double MIN_WEIGHT_FACTOR = 0.08;
inline constexpr double LOAD_SLOWDOWN_FACTOR = 0.9;
inline constexpr double MIN_TASK_SECONDS = 0.001;
inline constexpr double TASK_LOAD_FACTOR = 0.35;
inline constexpr double CONNECTION_LOAD_FACTOR = 0.04;
inline constexpr double MIN_TASK_PRESSURE_WEIGHT = 1.0;

class Server {
   public:
    using Duration = std::chrono::milliseconds;
    using Clock = std::chrono::steady_clock;

    Server(uint32_t weight)
        : id_(next_id_.fetch_add(1, std::memory_order_relaxed))
        , weight_(weight)
        , cnt_connects_(0)
        , total_request_(0)
        , total_time_(0)
        , last_update_(Clock::now()) {
        if (weight_ == 0) {
            throw std::invalid_argument("мощность сервера больше нуля");
        }
    }

    Duration runTask(const Task& task) {
        const Duration planned_duration = startTask(task);

        auto start = Clock::now();
        std::this_thread::sleep_for(planned_duration);
        auto end = Clock::now();

        auto duration = std::chrono::duration_cast<Duration>(end - start);
        finishTask(duration);

        return duration;
    }

    Duration estimateTaskDuration(const Task& task) const {
        std::lock_guard lock(state_mtx_);
        refreshLoadLocked(Clock::now());
        return estimateDurationLocked(task);
    }

    double getLoad() const {
        std::lock_guard lock(state_mtx_);
        refreshLoadLocked(Clock::now());
        return load_;
    }

    uint64_t getTotalRequests() const {
        return total_request_.load(std::memory_order_relaxed);
    }

    Duration getTotalTime() const {
        return Duration(total_time_.load(std::memory_order_relaxed));
    }

    void resetStats() {
        total_request_.store(0, std::memory_order_relaxed);
        total_time_.store(0, std::memory_order_relaxed);
    }

   private:
    uint64_t id_;
    uint32_t weight_;
    std::atomic<uint32_t> cnt_connects_{0};
    std::atomic<uint64_t> total_request_{0};
    std::atomic<uint64_t> total_time_{0};

    inline static std::atomic<uint64_t> next_id_{0};

    mutable std::mutex state_mtx_;
    mutable double load_ = 0.0;
    mutable Clock::time_point last_update_;

    Duration startTask(const Task& task) {
        std::lock_guard lock(state_mtx_);
        refreshLoadLocked(Clock::now());

        const uint32_t active_after_start = cnt_connects_.load(std::memory_order_relaxed) + 1;
        const Duration planned_duration = estimateDurationLocked(task);
        load_ = loadAfterTaskStartLocked(task, active_after_start);
        cnt_connects_.fetch_add(1, std::memory_order_relaxed);

        return planned_duration;
    }

    void finishTask(Duration measured_duration) {
        cnt_connects_.fetch_sub(1, std::memory_order_relaxed);

        {
            std::lock_guard lock(state_mtx_);
            refreshLoadLocked(Clock::now());
        }

        total_time_.fetch_add(static_cast<uint64_t>(measured_duration.count()), std::memory_order_relaxed);
        total_request_.fetch_add(1, std::memory_order_relaxed);
    }

    void refreshLoadLocked(Clock::time_point now) const {
        if (now <= last_update_) {
            return;
        }

        auto elapsed = now - last_update_;
        load_ = decayLoadLocked(elapsed);
        last_update_ = now;
    }

    //TODO: оценить корректность формулы на практике(собрать стату и валидировать)
    double decayLoadLocked(Clock::duration elapsed) const {
        double seconds = std::chrono::duration<double>(elapsed).count();

        double recovery = seconds * static_cast<double>(weight_) * LOAD_RECOVERY_RATE;

        return std::clamp(load_ - recovery, MIN_LOAD, MAX_LOAD);
    }

    //TODO: оценить корректность формулы на практике(собрать стату и валидировать)
    Duration estimateDurationLocked(const Task& task) const {
        double load_penalty = MAX_LOAD - load_ * LOAD_SLOWDOWN_FACTOR;

        double available_weight = static_cast<double>(weight_) * std::max(MIN_WEIGHT_FACTOR, load_penalty);

        double seconds = static_cast<double>(task.getCost()) / available_weight;

        auto duration = std::chrono::duration<double>(std::max(seconds, MIN_TASK_SECONDS));

        return std::chrono::ceil<Duration>(duration);
    }

    //TODO: оценить корректность формулы на практике(собрать стату и валидировать)
    double loadAfterTaskStartLocked(const Task& task, uint32_t active_connections) const {
        double task_pressure = static_cast<double>(task.getCost()) / std::max(MIN_TASK_PRESSURE_WEIGHT, static_cast<double>(weight_));

        double connection_pressure = CONNECTION_LOAD_FACTOR * static_cast<double>(active_connections);

        return std::clamp(load_ + task_pressure * TASK_LOAD_FACTOR + connection_pressure, MIN_LOAD, MAX_LOAD);
    }

   public:
    uint64_t getId() const {
        return id_;
    }

    uint32_t getWeight() const {
        return weight_;
    }

    uint32_t getConnects() const {
        return cnt_connects_.load(std::memory_order_relaxed);
    }
};

using ServerPtr = std::shared_ptr<Server>;

}  // namespace load_balancer
