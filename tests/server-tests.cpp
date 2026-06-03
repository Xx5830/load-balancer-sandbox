#include <gtest/gtest.h>

#include <sys/wait.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

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

namespace {
bool RejectedZeroWeight(int status) {
    if (WIFSIGNALED(status)) {
        return true;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 42;
    }
    return false;
}
}  // namespace

TEST(ServerWeight, ZeroWeightIsRejected) {
    EXPECT_EXIT(
        {
            try {
                Server s(0);
                (void)s;
            } catch (...) {
                std::exit(42);
            }
            std::exit(0);
        },
        RejectedZeroWeight,
        ".*");
}
