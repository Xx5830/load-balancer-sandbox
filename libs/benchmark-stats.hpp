#pragma once

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace load_balancer {

struct TimelineEntry {
    int t_ms = 0;
    int active_connections = 0;
    int requests_in_flight = 0;
    int new_requests = 0;
    int successful = 0;
    int failed_this_second = 0;
    double avg_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double total_load_avg = 0.0;
};

struct ServerResult {
    uint64_t id;
    uint32_t weight;
    double capacity;
    uint32_t max_parallel_requests;
    int start_at_ms;
    int crash_at_ms;
    uint64_t requests_received;
    uint64_t successful;
    uint64_t failed;
    double total_time_processing_ms;
    double avg_time_ms;
    double min_time_ms;
    double max_time_ms;
    double avg_load;
    double peak_load;
    uint64_t crashes;
};

struct ClientGroupResult {
    int group_index;
    std::string sticky_scope;
    int num_clients;
    int requests_sent = 0;
    int successful = 0;
    int failed_final = 0;
    int retries = 0;
    double latency_mean = 0.0;
    double latency_p95 = 0.0;
    double latency_max = 0.0;
};

struct BenchmarkStats {
    int total_requests = 0;
    int successful = 0;
    int failed = 0;
    int retries = 0;
    double actual_duration_ms = 0.0;
    double throughput_rps = 0.0;

    std::vector<double> latencies;

    double avg_latency_ms = 0.0;
    double stddev_latency_ms = 0.0;
    double p50_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double p99_latency_ms = 0.0;
    double min_latency_ms = 0.0;
    double max_latency_ms = 0.0;

    int server_crashed_failures = 0;
    int server_overloaded_failures = 0;
    int timeout_failures = 0;
    int unknown_failures = 0;

    std::vector<TimelineEntry> timeline;
    std::vector<ServerResult> servers;
    std::vector<ClientGroupResult> client_groups;

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
        out["latency"]["stddev"] = stddev_latency_ms;
        out["latency"]["percentiles"]["p50"] = p50_latency_ms;
        out["latency"]["percentiles"]["p95"] = p95_latency_ms;
        out["latency"]["percentiles"]["p99"] = p99_latency_ms;

        size_t bucket_count = 5;
        double bucket_size = 0.0;
        std::vector<size_t> latency_buckets(bucket_count, 0);

        if (!latencies.empty()) {
            bucket_size = (max_latency_ms - min_latency_ms) / static_cast<double>(bucket_count);
            if (bucket_size <= 0.0) {
                bucket_size = 1.0;
                latency_buckets[0] = latencies.size();
            } else {
                for (const double& latency : latencies) {
                    double latency_delta = latency - min_latency_ms;
                    size_t index = static_cast<size_t>(std::floor(latency_delta / bucket_size));
                    if (index >= bucket_count)
                        index = bucket_count - 1;
                    ++latency_buckets[index];
                }
            }
        }

        out["latency"]["histogram"] = {{"bucket_size_ms", bucket_size}, {"buckets", latency_buckets}};

        out["failures"]["total_final_failures"] = failed;
        out["failures"]["by_reason"]["server_crashed"] = server_crashed_failures;
        out["failures"]["by_reason"]["server_overloaded"] = server_overloaded_failures;
        out["failures"]["by_reason"]["timeout"] = timeout_failures;

        out["timeline"] = nlohmann::json::array();
        for (const auto& t : timeline) {
            out["timeline"].push_back({{"t_ms", t.t_ms},
                                       {"active_connections", t.active_connections},
                                       {"requests_in_flight", t.requests_in_flight},
                                       {"new_requests", t.new_requests},
                                       {"successful", t.successful},
                                       {"failed_this_second", t.failed_this_second},
                                       {"avg_latency_ms", t.avg_latency_ms},
                                       {"p95_latency_ms", t.p95_latency_ms},
                                       {"total_load_avg", t.total_load_avg}});
        }

        out["servers"] = nlohmann::json::array();
        for (const auto& s : servers) {
            nlohmann::json srv_json = {{"id", s.id},
                                       {"weight", s.weight},
                                       {"capacity", s.capacity},
                                       {"max_parallel_requests", s.max_parallel_requests},
                                       {"start_at_ms", s.start_at_ms},
                                       {"requests_received", s.requests_received},
                                       {"successful", s.successful},
                                       {"failed", s.failed},
                                       {"total_time_processing_ms", s.total_time_processing_ms},
                                       {"avg_time_ms", s.avg_time_ms},
                                       {"min_time_ms", s.min_time_ms},
                                       {"max_time_ms", s.max_time_ms},
                                       {"avg_load", s.avg_load},
                                       {"peak_load", s.peak_load},
                                       {"crashes", s.crashes}};
            if (s.crash_at_ms >= 0) {
                srv_json["crash_at_ms"] = s.crash_at_ms;
            } else {
                srv_json["crash_at_ms"] = nullptr;
            }
            out["servers"].push_back(srv_json);
        }

        out["client_groups"] = nlohmann::json::array();
        for (const auto& cg : client_groups) {
            out["client_groups"].push_back({{"group_index", cg.group_index},
                                            {"sticky_scope", cg.sticky_scope},
                                            {"num_clients", cg.num_clients},
                                            {"requests_sent", cg.requests_sent},
                                            {"successful", cg.successful},
                                            {"failed_final", cg.failed_final},
                                            {"retries", cg.retries},
                                            {"latency", {{"mean", cg.latency_mean}, {"p95", cg.latency_p95}, {"max", cg.latency_max}}}});
        }

        return out;
    }
};

}  // namespace load_balancer