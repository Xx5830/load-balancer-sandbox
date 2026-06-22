#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

#include "asio/io_context.hpp"
#include "benchmark.hpp"
#include "preset-parser.hpp"

using namespace load_balancer;
using json = nlohmann::json;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <preset.json> [output.json]\n";
        return 1;
    }

    std::ifstream ifs(argv[1]);
    if (!ifs) {
        std::cerr << "Cannot open file: " << argv[1] << '\n';
        return 1;
    }
    json preset;
    ifs >> preset;

    BenchmarkConfig config = parseConfig(preset);

    asio::io_context io;

    Benchmark benchmark(io, config);
    BenchmarkStats stats = benchmark.run();

    json out = stats.toJson();
    out["experiment"]["name"] = config.name;
    out["experiment"]["algorithm"] = (config.algorithm == Algorithm::RoundRobin         ? "RoundRobin"
                                      : config.algorithm == Algorithm::WeightRoundRobin ? "WeightRoundRobin"
                                      : config.algorithm == Algorithm::LeastConnections ? "LeastConnections"
                                                                                        : "ConsistentHashing");
    out["experiment"]["profile"] = config.profile;
    out["experiment"]["preset_file"] = argv[1];

    json cfg_json = preset;
    cfg_json["duration_ms"] = config.duration_ms;
    cfg_json["warmup_ms"] = config.warmup_ms;
    cfg_json["total_request_limit"] = config.total_request_limit;
    cfg_json["manager_workers"] = config.manager_workers;
    cfg_json["seed"] = config.seed;

    cfg_json["server_model_params"]["load_recovery_rate"] = config.server_model_params.load_recovery_rate_;
    cfg_json["server_model_params"]["load_slowdown_factor"] = config.server_model_params.load_slowdown_factor_;
    cfg_json["server_model_params"]["task_load_factor"] = config.server_model_params.task_load_factor_;
    cfg_json["server_model_params"]["connection_load_factor"] = config.server_model_params.connection_load_factor_;
    cfg_json["server_model_params"]["min_task_seconds"] = config.server_model_params.min_task_seconds_;
    cfg_json["server_model_params"]["reject_threshold_seconds"] = config.server_model_params.reject_threshold_seconds_;
    cfg_json["server_model_params"]["overload_reject_factor"] = config.server_model_params.overload_reject_factor_;
    cfg_json["server_model_params"]["min_weight_factor"] = config.server_model_params.min_weight_factor_;

    for (size_t i = 0; i < config.servers.size(); ++i) {
        cfg_json["servers"][i]["weight"] = config.servers[i].weight;
        cfg_json["servers"][i]["capacity"] = config.servers[i].capacity;
        cfg_json["servers"][i]["max_parallel_requests"] = config.servers[i].max_parallel_requests;
        cfg_json["servers"][i]["start_at_ms"] = config.servers[i].start_at_ms;
        if (config.servers[i].crash_at_ms >= 0) {
            cfg_json["servers"][i]["crash_at_ms"] = config.servers[i].crash_at_ms;
        } else {
            cfg_json["servers"][i]["crash_at_ms"] = nullptr;
        }
    }

    for (size_t i = 0; i < config.client_groups.size(); ++i) {
        cfg_json["client_groups"][i]["count"] = config.client_groups[i].count;
        cfg_json["client_groups"][i]["max_requests"] = config.client_groups[i].max_requests;
        cfg_json["client_groups"][i]["sticky_scope"] =
            (config.client_groups[i].sticky_scope == StickyScope::None
                 ? "none"
                 : (config.client_groups[i].sticky_scope == StickyScope::Client ? "client" : "group"));
    }

    out["config"] = cfg_json;

    if (argc >= 3) {
        std::ofstream ofs(argv[2]);
        if (!ofs) {
            std::cerr << "Cannot write to file: " << argv[2] << '\n';
            return 1;
        }
        ofs << out.dump(2) << '\n';
    } else {
        std::cout << out.dump(2) << '\n';
    }
    return 0;
}