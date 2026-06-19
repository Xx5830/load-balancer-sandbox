#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace load_balancer {

struct BenchmarkStats {
    int total_requests = 0;
    int successful = 0;
    int failed = 0;
    int retries = 0;
    double actual_duration_ms = 0.0;
    double throughput_rps = 0.0;

    std::vector<double> latencies;

    double avg_latency_ms = 0.0;
    double p50_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double p99_latency_ms = 0.0;
    double min_latency_ms = 0.0;
    double max_latency_ms = 0.0;

    int server_crashed_failures = 0;
    int server_overloaded_failures = 0;
    int timeout_failures = 0;
    int unknown_failures = 0;

    nlohmann::json toJson() const {
        nlohmann::json out;

        out["totals"]["requests_sent"] = total_requests;
        out["totals"]["successful"] = successful;
        out["totals"]["failed_final"] = failed;
        out["totals"]["retries"] = retries;
        out["totals"]["actual_duration_ms"] = actual_duration_ms;
        out["totals"]["throughput_rps"] = throughput_rps;

        out["latency"]["unit"] = "ms";
        out["latency"]["count"] = latencies.size();
        out["latency"]["min"] = min_latency_ms;
        out["latency"]["max"] = max_latency_ms;
        out["latency"]["mean"] = avg_latency_ms;
        out["latency"]["stddev"] = 0.0;
        out["latency"]["percentiles"]["p50"] = p50_latency_ms;
        out["latency"]["percentiles"]["p95"] = p95_latency_ms;
        out["latency"]["percentiles"]["p99"] = p99_latency_ms;
        out["latency"]["histogram"] = {{"bucket_size_ms", 0.0}, {"buckets", nlohmann::json::array()}};

        out["failures"]["total_final_failures"] = failed;
        out["failures"]["by_reason"]["server_crashed"] = server_crashed_failures;
        out["failures"]["by_reason"]["server_overloaded"] = server_overloaded_failures;
        out["failures"]["by_reason"]["timeout"] = timeout_failures;

        out["timeline"] = nlohmann::json::array();
        out["servers"] = nlohmann::json::array();
        out["client_groups"] = nlohmann::json::array();

        return out;
    }
};

}  // namespace load_balancer