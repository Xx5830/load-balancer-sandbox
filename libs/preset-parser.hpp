#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark-config.hpp"
#include "generators.hpp"

namespace load_balancer {

inline GeneratorPtr parseGenerator(const nlohmann::json& j) {
    if (j.is_array()) {
        std::vector<double> vals = j.get<std::vector<double>>();
        return std::make_shared<SequenceGenerator>(vals);
    }
    if (!j.is_object()) {
        throw std::runtime_error("Invalid generator: expected object or array");
    }

    std::string type = j.at("type");
    if (type == "sequence") {
        std::vector<double> vals = j.at("values").get<std::vector<double>>();

        return std::make_shared<SequenceGenerator>(vals);
    } else if (type == "uniform") {
        double mn = j.at("min");
        double mx = j.at("max");

        return std::make_shared<UniformGenerator>(mn, mx);
    } else if (type == "normal") {
        std::optional<double> min, max;

        if (j.contains("min")) {
            min = j["min"];
        }
        if (j.contains("max")) {
            max = j["max"];
        }

        return std::make_shared<NormalGenerator>(j.at("center"), j.at("deviation"), min, max);
    } else if (type == "exponential") {
        return std::make_shared<ExponentialGenerator>(j.at("center"));
    } else if (type == "lognormal") {
        std::optional<double> min, max;

        if (j.contains("min")) {
            min = j["min"];
        }
        if (j.contains("max")) {
            max = j["max"];
        }

        return std::make_shared<LognormalGenerator>(j.at("center"), j.at("deviation"), min, max);
    }

    throw std::runtime_error("Unknown generator type: " + type);
}

inline BenchmarkConfig parseConfig(const nlohmann::json& preset) {
    BenchmarkConfig cfg;
    cfg.name = preset.value("name", "experiment");

    std::string algostr = preset.value("algorithm", "RoundRobin");
    if (algostr == "RoundRobin") {
        cfg.algorithm = Algorithm::RoundRobin;
    } else if (algostr == "WeightRoundRobin") {
        cfg.algorithm = Algorithm::WeightRoundRobin;
    } else if (algostr == "LeastConnections") {
        cfg.algorithm = Algorithm::LeastConnections;
    } else if (algostr == "ConsistentHashing") {
        cfg.algorithm = Algorithm::ConsistentHashing;
    } else {
        throw std::runtime_error("Unknown algorithm: " + algostr);
    }

    cfg.profile = preset.value("profile", "custom");
    cfg.duration_ms = preset.value("duration_ms", 30000);
    cfg.warmup_ms = preset.value("warmup_ms", 0);
    cfg.total_request_limit = preset.value("total_request_limit", 0);
    cfg.manager_workers = preset.value("manager_workers", 0);
    cfg.seed = preset.value("seed", 42);

    if (preset.contains("server_model_params")) {
        auto& p = preset["server_model_params"];
        cfg.server_model_params.load_recovery_rate_ = p.value("load_recovery_rate", 0.25);
        cfg.server_model_params.load_slowdown_factor_ = p.value("load_slowdown_factor", 0.9);
        cfg.server_model_params.task_load_factor_ = p.value("task_load_factor", 0.35);
        cfg.server_model_params.connection_load_factor_ = p.value("connection_load_factor", 0.04);
        cfg.server_model_params.min_task_seconds_ = p.value("min_task_seconds", 0.001);
        cfg.server_model_params.reject_threshold_seconds_ = p.value("reject_threshold_seconds", 5.0);
        cfg.server_model_params.overload_reject_factor_ = p.value("overload_reject_factor", 1.0);
        cfg.server_model_params.min_weight_factor_ = p.value("min_weight_factor", 0.08);
    }

    if (!preset.contains("servers") || !preset["servers"].is_array()) {
        throw std::runtime_error("No servers defined");
    }

    for (const auto& srv : preset["servers"]) {
        ServerConfig s;
        s.weight = srv.at("weight");
        s.capacity = srv.value("capacity", 1.0);
        s.max_parallel_requests = srv.value("max_parallel_requests", 1);
        s.background_load_gen = nullptr;
        s.start_at_ms = srv.value("start_at_ms", 0);
        s.crash_at_ms = srv.value("crash_at_ms", -1);
        cfg.servers.push_back(s);
    }

    if (!preset.contains("client_groups") || !preset["client_groups"].is_array()) {
        throw std::runtime_error("No client_groups defined");
    }

    for (const auto& grp : preset["client_groups"]) {
        ClientGroupConfig g;
        g.count = grp.at("count");

        std::string scope = grp.value("sticky_scope", "none");
        if (scope == "none") {
            g.sticky_scope = StickyScope::None;
        } else if (scope == "client") {
            g.sticky_scope = StickyScope::Client;
        } else if (scope == "group") {
            g.sticky_scope = StickyScope::Group;
        } else {
            throw std::runtime_error("Unknown sticky_scope: " + scope);
        }

        g.inter_arrival_gen = parseGenerator(grp.at("inter_arrival_ms"));
        g.task_cost_gen = parseGenerator(grp.at("task_cost"));
        g.max_requests = grp.value("max_requests", 0);

        if (grp.contains("retry_policy")) {
            auto& r = grp["retry_policy"];
            g.retry.max_retries = r.value("max_retries", 0);
            g.retry.delay_gen = parseGenerator(r.value("delay_ms", nlohmann::json::object({{"type", "constant"}, {"value", 0}})));
            g.retry.timeout_ms = r.value("timeout_ms", 0);
        } else {
            g.retry.max_retries = 0;
            double seq[1]{0.0};
            g.retry.delay_gen = std::make_shared<SequenceGenerator>(seq);
            g.retry.timeout_ms = 0;
        }

        if (grp.contains("active_windows")) {
            for (const auto& w : grp["active_windows"]) {
                g.active_windows.emplace_back(w.at("start_ms"), w.at("end_ms"));
            }
        }

        cfg.client_groups.push_back(std::move(g));
    }
    return cfg;
}

}  // namespace load_balancer