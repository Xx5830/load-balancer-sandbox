#include "server.hpp"

#include <bits/chrono.h>
#include <gtest/gtest.h>

#include "task.hpp"

using namespace std::chrono_literals;
using load_balancer::Server;
using load_balancer::Task;

TEST(ServerTaskExecution, RunTaskTakesAtLeastEstimatedTime) {
    Server server(1);
    Task task(1, 0.03L);

    auto expected = server.estimateTaskDuration(task);
    auto elapsed = server.runTask(task);

    EXPECT_GE(elapsed, expected);
    EXPECT_LT(elapsed, expected + 500ms);
}

TEST(ServerTaskExecution, ZeroCostTaskReturnsQuickly) {
    Server server(1);
    Task task(1, 0.0L);

    auto elapsed = server.runTask(task);

    EXPECT_LT(elapsed, 100ms);
}

TEST(ServerTaskExecution, UpdatedTaskCostIsUsedByRunTask) {
    Server server(1);
    Task task(1, 0.001L);
    task.setCost(0.02L);

    auto expected = server.estimateTaskDuration(task);
    auto elapsed = server.runTask(task);

    EXPECT_GE(elapsed, expected);
    EXPECT_LT(elapsed, expected + 500ms);
}
