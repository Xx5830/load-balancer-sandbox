#include <gtest/gtest.h>

#include <memory>

#include "load-balancing.hpp"
#include "manager.hpp"
#include "pick-policy.hpp"
#include "server.hpp"
#include "task.hpp"

using load_balancer::LoadBalancer;
using load_balancer::RoundRobinPick;
using load_balancer::Server;
using load_balancer::ServerManager;
using load_balancer::ServerPtr;
using load_balancer::Task;

namespace {

ServerPtr makeServer(uint32_t weight = 1) {
    return std::make_shared<Server>(weight, 1.0, 1);
}

}  // namespace

TEST(CrashedServerScope, RoundRobinSkipsCrashedServer) {
    auto mgr = std::make_shared<ServerManager>(std::make_unique<LoadBalancer>(), 0);
    mgr->setPickPolicy<RoundRobinPick>();

    auto alive = makeServer();
    auto dead = makeServer();
    alive->markStarted();
    dead->markStarted();
    mgr->addServer(alive);
    mgr->addServer(dead);

    dead->crash();

    RoundRobinPick policy;
    policy.resetServers(mgr->servers());
    auto servers = mgr->servers();
    for (int i = 0; i < 100; ++i) {
        auto picked = policy.pickServer(static_cast<uint64_t>(i), servers);
        ASSERT_TRUE(picked.has_value());
        EXPECT_NE((*picked)->getId(), dead->getId()) << "RoundRobin отдал крашнутый сервер на итерации " << i;
    }
}

TEST(CrashedServerScope, ManagerRoutesAroundCrashedServer) {
    auto mgr = std::make_shared<ServerManager>(std::make_unique<LoadBalancer>(), 0);
    mgr->setPickPolicy<RoundRobinPick>();

    auto alive = makeServer();
    auto dead = makeServer();
    alive->markStarted();
    dead->markStarted();
    mgr->addServer(alive);
    mgr->addServer(dead);

    dead->crash();

    int crashed_failures = 0;
    for (int i = 0; i < 50; ++i) {
        auto fut = mgr->submitTask(Task(static_cast<uint64_t>(i), 0.05));
        try {
            fut.get();
        } catch (const load_balancer::ServerCrashed&) {
            ++crashed_failures;
        } catch (...) {
        }
    }

    EXPECT_EQ(crashed_failures, 0) << "Запросы попали в крашнутый сервер: он не убран из скоупа выбора";
    EXPECT_EQ(dead->getStats().requests_received_, 0u) << "Крашнутый сервер получил запросы после краша";
}
