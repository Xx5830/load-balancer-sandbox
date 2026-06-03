#include <gtest/gtest.h>

#include <sys/wait.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "server.hpp"

using namespace std::chrono_literals;
using load_balancer::Server;
using load_balancer::Task;

TEST(ServerBasic, WeightIsStored) {
    Server s(5);
    EXPECT_EQ(s.getWeight(), 5u);
}

TEST(ServerBasic, IdsAreUnique) {
    Server a(1);
    Server b(1);
    EXPECT_NE(a.getId(), b.getId());
}

TEST(ServerBasic, NoConnectsInitially) {
    Server s(1);
    EXPECT_EQ(s.getConnects(), 0u);
}

TEST(ServerBasic, RunTaskReturnsAtLeastTaskTime) {
    Server s(1);
    Task t(0, 1.0L, 30ms);

    auto elapsed = s.runTask(t);

    EXPECT_GE(elapsed, 30ms);
    EXPECT_EQ(s.getConnects(), 0u);
}

TEST(ServerConcurrency, IdsAreUniqueAcrossThreads) {
    constexpr int kThreads = 10;
    constexpr int kPerThread = 1000;

    std::vector<std::vector<uint64_t>> per_thread(kThreads);

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&per_thread, t] {
                auto& ids = per_thread[t];
                ids.reserve(kPerThread);
                for (int i = 0; i < kPerThread; ++i) {
                    Server s(1);
                    ids.push_back(s.getId());
                }
            });
        }
    }

    std::vector<uint64_t> ids;
    ids.reserve(kThreads * kPerThread);
    for (auto& chunk : per_thread) {
        ids.insert(ids.end(), chunk.begin(), chunk.end());
    }

    ASSERT_EQ(ids.size(), static_cast<size_t>(kThreads * kPerThread));

    std::sort(ids.begin(), ids.end());
    auto dup = std::adjacent_find(ids.begin(), ids.end());
    EXPECT_EQ(dup, ids.end()) << "duplicate server id: " << *dup;
}
