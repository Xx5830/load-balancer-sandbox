#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "task.hpp"

using namespace std::chrono_literals;
using load_balancer::Task;

TEST(TaskBasic, ConstructorStoresFields) {
    Task t(42, 3.5L, 100ms);

    EXPECT_EQ(t.getId(), 42u);
    EXPECT_DOUBLE_EQ(static_cast<double>(t.getCost()), 3.5);
    EXPECT_EQ(t.time(), std::chrono::steady_clock::duration(100ms));
}

TEST(TaskBasic, SetId) {
    Task t(1, 0.0L, 0ms);

    t.setId(7);
    EXPECT_EQ(t.getId(), 7u);
}

TEST(TaskBasic, SetCost) {
    Task t(1, 0.0L, 0ms);

    t.setCost(12.25L);
    EXPECT_DOUBLE_EQ(static_cast<double>(t.getCost()), 12.25);
}

TEST(TaskBasic, SetTime) {
    Task t(1, 0.0L, 0ms);

    t.setTime(250ms);
    EXPECT_EQ(t.time(), std::chrono::steady_clock::duration(250ms));
}

TEST(TaskBasic, TimeIsStoredNotMeasured) {
    Task t(1, 0.0L, 500ms);

    std::this_thread::sleep_for(10ms);

    EXPECT_EQ(t.time(), std::chrono::steady_clock::duration(500ms));
}

TEST(TaskSleep, TimeFitsSleepFor) {
    constexpr auto execution = 50ms;
    Task t(1, 1.0L, execution);

    const auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(t.time());
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_GE(elapsed, execution);
    EXPECT_LT(elapsed, execution + 500ms);
}

TEST(TaskSleep, ZeroDurationReturnsImmediately) {
    Task t(1, 1.0L, 0ms);

    const auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(t.time());
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_LT(elapsed, 100ms);
}

TEST(TaskSleep, UpdatedTimeIsUsedBySleepFor) {
    Task t(1, 1.0L, 0ms);
    t.setTime(40ms);

    const auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(t.time());
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_GE(elapsed, 40ms);
}
