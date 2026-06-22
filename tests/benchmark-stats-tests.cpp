#include <gtest/gtest.h>

#include "benchmark-stats.hpp"

using load_balancer::BenchmarkStats;

// toJson переносит итоговые счётчики в секцию totals.
TEST(BenchmarkStats, TotalsAreSerialized) {
    BenchmarkStats s;
    s.total_requests = 100;
    s.successful = 90;
    s.failed = 10;
    s.retries = 5;
    s.actual_duration_ms = 1500.0;
    s.throughput_rps = 60.0;

    auto j = s.toJson();
    EXPECT_EQ(j["totals"]["requests_sent"], 100);
    EXPECT_EQ(j["totals"]["successful"], 90);
    EXPECT_EQ(j["totals"]["failed_final"], 10);
    EXPECT_EQ(j["totals"]["retries"], 5);
    EXPECT_DOUBLE_EQ(j["totals"]["actual_duration_ms"].get<double>(), 1500.0);
    EXPECT_DOUBLE_EQ(j["totals"]["throughput_rps"].get<double>(), 60.0);
}

// toJson переносит латентности и перцентили в секцию latency.
TEST(BenchmarkStats, LatencySectionIsSerialized) {
    BenchmarkStats s;
    s.latencies = {1.0, 2.0, 3.0};
    s.min_latency_ms = 1.0;
    s.max_latency_ms = 3.0;
    s.avg_latency_ms = 2.0;
    s.p50_latency_ms = 2.0;
    s.p95_latency_ms = 3.0;
    s.p99_latency_ms = 3.0;

    auto j = s.toJson();
    EXPECT_EQ(j["latency"]["unit"], "ms");
    EXPECT_EQ(j["latency"]["count"], 3u);
    EXPECT_DOUBLE_EQ(j["latency"]["min"].get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(j["latency"]["max"].get<double>(), 3.0);
    EXPECT_DOUBLE_EQ(j["latency"]["mean"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(j["latency"]["percentiles"]["p50"].get<double>(), 2.0);
}

// toJson раскладывает отказы по причинам.
TEST(BenchmarkStats, FailuresByReasonAreSerialized) {
    BenchmarkStats s;
    s.failed = 7;
    s.server_crashed_failures = 2;
    s.server_overloaded_failures = 3;
    s.timeout_failures = 1;

    auto j = s.toJson();
    EXPECT_EQ(j["failures"]["total_final_failures"], 7);
    EXPECT_EQ(j["failures"]["by_reason"]["server_crashed"], 2);
    EXPECT_EQ(j["failures"]["by_reason"]["server_overloaded"], 3);
    EXPECT_EQ(j["failures"]["by_reason"]["timeout"], 1);
}

// У пустой статистики секции timeline/servers/client_groups — пустые массивы.
TEST(BenchmarkStats, EmptyCollectionsAreArrays) {
    BenchmarkStats s;
    auto j = s.toJson();
    EXPECT_TRUE(j["timeline"].is_array());
    EXPECT_TRUE(j["servers"].is_array());
    EXPECT_TRUE(j["client_groups"].is_array());
    EXPECT_EQ(j["timeline"].size(), 0u);
}
