#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "generators.hpp"
#include "pick-policy.hpp"
#include "server.hpp"

namespace load_balancer {

enum class StickyScope { None, Client, Group };

struct ServerConfig {
    uint32_t weight;
    double capacity = 1.0;
    uint32_t max_parallel_requests = 1;
    GeneratorPtr background_load_gen;
    int start_at_ms = 0;
    int crash_at_ms = -1;
};

struct ClientGroupConfig {
    int count;
    StickyScope sticky_scope = StickyScope::None;
    GeneratorPtr inter_arrival_gen;
    GeneratorPtr task_cost_gen;
    int max_requests = 0;
    struct {
        int max_retries = 0;
        GeneratorPtr delay_gen;
        int timeout_ms = 0;
    } retry;
    std::vector<std::pair<int, int>> active_windows;
};

struct BenchmarkConfig {
    std::string name;
    Algorithm algorithm = Algorithm::RoundRobin;
    std::string profile = "custom";
    int duration_ms = 30000;
    int warmup_ms = 0;
    int total_request_limit = 0;
    int manager_workers = 0;
    int seed = 42;
    std::vector<ServerConfig> servers;
    std::vector<ClientGroupConfig> client_groups;
    ServerModelParams server_model_params;
};

}  // namespace load_balancer