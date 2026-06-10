#include <gtest/gtest.h>

#include "task.hpp"

using load_balancer::Task;

TEST(TaskBasic, ConstructorStoresFields) {
    Task task(42, 3.5L);

    EXPECT_EQ(task.getId(), 42u);
    EXPECT_DOUBLE_EQ(static_cast<double>(task.getCost()), 3.5);
}

TEST(TaskBasic, SetId) {
    Task task(1, 0.0L);

    task.setId(7);
    EXPECT_EQ(task.getId(), 7u);
    EXPECT_DOUBLE_EQ(static_cast<double>(task.getCost()), 0.0);
}

TEST(TaskBasic, SetCost) {
    Task task(1, 0.0L);

    task.setCost(12.25L);
    EXPECT_DOUBLE_EQ(static_cast<double>(task.getCost()), 12.25);
    EXPECT_EQ(task.getId(), 1u);
}

TEST(TaskBasic, UpdatedIdIsStored) {
    Task task(1, 3.5L);

    task.setId(99);
    EXPECT_EQ(task.getId(), 99u);
    EXPECT_DOUBLE_EQ(static_cast<double>(task.getCost()), 3.5);
}

TEST(TaskBasic, UpdatedCostIsStored) {
    Task task(1, 3.5L);

    task.setCost(7.75L);
    EXPECT_EQ(task.getId(), 1u);
    EXPECT_DOUBLE_EQ(static_cast<double>(task.getCost()), 7.75);
}
