#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <memory>
#include <vector>

#include "benchmark.hpp"
#include "benchmark-config.hpp"
#include "generators.hpp"
#include "pick-policy.hpp"

using load_balancer::Algorithm;
using load_balancer::Benchmark;
using load_balancer::BenchmarkConfig;
using load_balancer::ClientGroupConfig;
using load_balancer::SequenceGenerator;
using load_balancer::ServerConfig;

namespace {

// Короткий прогон: 1 сервер, 1 клиент, RoundRobin, длительность 100 мс.
BenchmarkConfig tinyConfig() {
    BenchmarkConfig cfg;
    cfg.algorithm = Algorithm::RoundRobin;
    cfg.duration_ms = 100;
    cfg.seed = 1;

    ServerConfig srv;
    srv.weight = 1;
    srv.capacity = 10.0;
    srv.max_parallel_requests = 2;
    cfg.servers.push_back(srv);

    ClientGroupConfig grp;
    grp.count = 1;
    double inter[1]{5.0};
    double cost[1]{0.1};
    grp.inter_arrival_gen = std::make_shared<SequenceGenerator>(inter);
    grp.task_cost_gen = std::make_shared<SequenceGenerator>(cost);
    double delay[1]{0.0};
    grp.retry.delay_gen = std::make_shared<SequenceGenerator>(delay);
    cfg.client_groups.push_back(std::move(grp));
    return cfg;
}

}  // namespace

// Прогон бенчмарка завершается и измеряет ненулевую длительность.
TEST(Benchmark, RunCompletesWithDuration) {
    asio::io_context io;
    Benchmark bench(io, tinyConfig());
    auto stats = bench.run();
    EXPECT_GT(stats.actual_duration_ms, 0.0);
}

// За время прогона отправляется хотя бы одна задача.
TEST(Benchmark, SendsRequests) {
    asio::io_context io;
    Benchmark bench(io, tinyConfig());
    auto stats = bench.run();
    EXPECT_GT(stats.total_requests, 0);
    EXPECT_EQ(stats.total_requests, stats.successful + stats.failed);
}

// При наличии успешных запросов считается ненулевая пропускная способность.
TEST(Benchmark, ThroughputComputedWhenSuccessful) {
    asio::io_context io;
    Benchmark bench(io, tinyConfig());
    auto stats = bench.run();
    if (stats.successful > 0) {
        EXPECT_GT(stats.throughput_rps, 0.0);
        EXPECT_EQ(stats.latencies.size(), static_cast<size_t>(stats.successful));
    }
}

// Глобальный total_request_limit ограничивает суммарные запросы всех клиентов.
TEST(Benchmark, RespectsTotalRequestLimit) {
    auto cfg = tinyConfig();
    cfg.duration_ms = 1000;
    cfg.total_request_limit = 3;
    cfg.client_groups[0].count = 4;

    asio::io_context io;
    Benchmark bench(io, cfg);
    auto stats = bench.run();

    EXPECT_LE(stats.total_requests, 3);
    EXPECT_EQ(stats.total_requests, stats.successful + stats.failed);
}
