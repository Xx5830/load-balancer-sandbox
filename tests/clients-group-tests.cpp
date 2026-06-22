#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <memory>

#include "clients-group.hpp"
#include "load-balancing.hpp"
#include "manager.hpp"
#include "pick-policy.hpp"

using load_balancer::ClientGroupConfig;
using load_balancer::ClientStats;
using load_balancer::LoadBalancer;
using load_balancer::RoundRobinPick;
using load_balancer::runClient;
using load_balancer::SequenceGenerator;
using load_balancer::ServerManager;

namespace {

ClientGroupConfig makeGroup() {
    ClientGroupConfig grp;
    grp.count = 1;
    double inter[1]{5.0};
    double cost[1]{0.1};
    grp.inter_arrival_gen = std::make_shared<SequenceGenerator>(inter);
    grp.task_cost_gen = std::make_shared<SequenceGenerator>(cost);
    double delay[1]{0.0};
    grp.retry.delay_gen = std::make_shared<SequenceGenerator>(delay);
    return grp;
}

ClientStats runOnce(ServerManager& mgr, const ClientGroupConfig& grp, int duration_ms) {
    asio::io_context io;
    ClientStats stats;
    auto start = std::chrono::steady_clock::now();
    asio::co_spawn(io, runClient(0, 0, grp, mgr, stats, start, duration_ms, 0, 1), asio::detached);
    io.run();
    return stats;
}

}  // namespace

// За окно работы клиент отправляет хотя бы один запрос.
TEST(RunClient, SendsRequests) {
    ServerManager mgr(std::make_unique<LoadBalancer>(), 0);
    mgr.setPickPolicy<RoundRobinPick>();
    mgr.addServer(1);

    auto stats = runOnce(mgr, makeGroup(), 100);
    EXPECT_GT(stats.requests_sent, 0);
    EXPECT_EQ(stats.requests_sent, stats.successful + stats.failed);
}

// Успешные запросы добавляют ровно одну латентность каждый.
TEST(RunClient, SuccessfulRequestsRecordLatency) {
    ServerManager mgr(std::make_unique<LoadBalancer>(), 0);
    mgr.setPickPolicy<RoundRobinPick>();
    mgr.addServer(1);

    auto stats = runOnce(mgr, makeGroup(), 100);
    EXPECT_EQ(stats.latencies.size(), static_cast<size_t>(stats.successful));
}

// max_requests ограничивает число отправленных запросов.
TEST(RunClient, RespectsMaxRequests) {
    ServerManager mgr(std::make_unique<LoadBalancer>(), 0);
    mgr.setPickPolicy<RoundRobinPick>();
    mgr.addServer(1);

    auto grp = makeGroup();
    grp.max_requests = 3;
    auto stats = runOnce(mgr, grp, 5000);
    EXPECT_LE(stats.requests_sent, 3);
}

// Один клиентский запрос отправляет на сервер ровно одну задачу.
TEST(RunClient, SubmitsOneServerTaskPerRequest) {
    ServerManager mgr(std::make_unique<LoadBalancer>(), 0);
    mgr.setPickPolicy<RoundRobinPick>();
    mgr.addServer(1);

    auto grp = makeGroup();
    grp.max_requests = 1;
    auto stats = runOnce(mgr, grp, 500);

    ASSERT_EQ(stats.requests_sent, 1);
    ASSERT_EQ(mgr.servers().size(), 1u);
    EXPECT_EQ(mgr.servers()[0]->getStats().requests_received_, 1u);
}
