#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

#include "benchmark.hpp"
#include "benchmark-config.hpp"
#include "generators.hpp"
#include "pick-policy.hpp"

using load_balancer::Algorithm;
using load_balancer::Benchmark;
using load_balancer::BenchmarkConfig;
using load_balancer::ClientGroupConfig;
using load_balancer::ConstantGenerator;
using load_balancer::ExponentialGenerator;
using load_balancer::ServerConfig;
using load_balancer::StickyScope;
using load_balancer::UniformGenerator;

namespace {

// Общие параметры прогона.
constexpr int kDurationMinMs = 1000; // минимальная длительность прогона
constexpr int kDurationMaxMs = 15000; // максимальная длительность прогона
constexpr int kManagerWorkersMin = 1; // минимум воркеров менеджера
constexpr int kManagerWorkersMax = 100; // максимум воркеров менеджера

// Серверы.
constexpr int kServerCountMin = 1; // минимум серверов
constexpr int kServerCountMax = 20; // максимум серверов
constexpr double kCapacityMin = 0.1; // минимум пропускной способности сервера (доля [0,1])
constexpr double kCapacityMax = 1.0; // максимум пропускной способности сервера (доля [0,1])
constexpr int kWeightMin = 1; // минимум веса сервера
constexpr int kWeightMax = 8; // максимум веса сервера
constexpr int kParallelMin = 1; // минимум параллельных запросов сервера
constexpr int kParallelMax = 8; // максимум параллельных запросов сервера
constexpr double kStartProb = 0.3; // вероятность отложенного старта сервера
constexpr int kStartAtMinMs = 50; // минимальная задержка старта
constexpr int kStartAtMaxMs = 1000; // максимальная задержка старта
constexpr double kCrashProb = 0.2; // вероятность краша сервера
constexpr int kCrashAtMinMs = 200; // минимальный момент краша
constexpr int kCrashAtMaxMs = 5000; // максимальный момент краша
constexpr double kBackgroundProb = 0.4; // вероятность фоновой нагрузки на сервере
constexpr double kBackgroundLoadMin = 0.0; // минимум фоновой нагрузки
constexpr double kBackgroundLoadMax = 1.0; // максимум фоновой нагрузки

// Клиентские группы.
constexpr int kGroupCountMin = 1; // минимум групп
constexpr int kGroupCountMax = 10; // максимум групп
constexpr int kGroupClientsMin = 1; // минимум клиентов в группе
constexpr int kGroupClientsMax = 20; // максимум клиентов в группе
constexpr double kInterArrivalRateMin = 0.1; // минимум интенсивности потока
constexpr double kInterArrivalRateMax = 2.0; // максимум интенсивности потока
constexpr double kTaskCostMin = 0.05; // минимум стоимости задачи
constexpr double kTaskCostMax = 1.0; // максимум стоимости задачи
constexpr int kMaxRetriesMin = 0; // минимум повторов
constexpr int kMaxRetriesMax = 5; // максимум повторов
constexpr double kRetryDelayMinMs = 1.0; // минимум задержки повтора
constexpr double kRetryDelayMaxMs = 50.0; // максимум задержки повтора
constexpr int kRetryTimeoutMinMs = 200; // минимум таймаута
constexpr int kRetryTimeoutMaxMs = 2000; // максимум таймаута

BenchmarkConfig heavyConfig(int seed) {
    std::mt19937 cfg_rng(static_cast<uint32_t>(seed));
    auto randInt = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(cfg_rng); };
    auto randDouble = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(cfg_rng); };
    auto chance = [&](double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(cfg_rng) < p; };

    BenchmarkConfig cfg;
    cfg.name = "heavy-stress";
    cfg.algorithm = Algorithm::RoundRobin;
    cfg.duration_ms = randInt(kDurationMinMs, kDurationMaxMs);
    cfg.seed = seed;
    cfg.manager_workers = randInt(kManagerWorkersMin, kManagerWorkersMax);

    int server_count = randInt(kServerCountMin, kServerCountMax);
    for (int i = 0; i < server_count; ++i) {
        ServerConfig srv;
        srv.weight = static_cast<uint32_t>(randInt(kWeightMin, kWeightMax));
        srv.capacity = randDouble(kCapacityMin, kCapacityMax);
        srv.max_parallel_requests = static_cast<uint32_t>(randInt(kParallelMin, kParallelMax));
        if (chance(kStartProb)) srv.start_at_ms = randInt(kStartAtMinMs, kStartAtMaxMs);
        if (chance(kCrashProb)) srv.crash_at_ms = randInt(kCrashAtMinMs, kCrashAtMaxMs);
        if (chance(kBackgroundProb)) {
            double lo = randDouble(kBackgroundLoadMin, kBackgroundLoadMax);
            double hi = randDouble(lo, kBackgroundLoadMax);
            srv.background_load_gen = std::make_shared<UniformGenerator>(lo, hi);
        }
        cfg.servers.push_back(srv);
    }

    const StickyScope scopes[3] = {StickyScope::None, StickyScope::Client, StickyScope::Group};
    int group_count = randInt(kGroupCountMin, kGroupCountMax);
    for (int g = 0; g < group_count; ++g) {
        ClientGroupConfig grp;
        grp.count = randInt(kGroupClientsMin, kGroupClientsMax);
        grp.sticky_scope = scopes[randInt(0, 2)];
        grp.inter_arrival_gen = std::make_shared<ExponentialGenerator>(randDouble(kInterArrivalRateMin, kInterArrivalRateMax));
        grp.task_cost_gen = std::make_shared<ConstantGenerator>(randDouble(kTaskCostMin, kTaskCostMax));
        grp.retry.max_retries = randInt(kMaxRetriesMin, kMaxRetriesMax);
        grp.retry.delay_gen = std::make_shared<ConstantGenerator>(randDouble(kRetryDelayMinMs, kRetryDelayMaxMs));
        grp.retry.timeout_ms = randInt(kRetryTimeoutMinMs, kRetryTimeoutMaxMs);
        cfg.client_groups.push_back(std::move(grp));
    }
    return cfg;
}

int seedFromEnv() {
    const char* e = std::getenv("STRESS_SEED");
    return e ? std::atoi(e) : 0;
}

const char* algoName(Algorithm a) {
    switch (a) {
        case Algorithm::RoundRobin: return "RoundRobin";
        case Algorithm::WeightRoundRobin: return "WeightRoundRobin";
        case Algorithm::LeastConnections: return "LeastConnections";
        case Algorithm::ConsistentHashing: return "ConsistentHashing";
    }
    return "RoundRobin";
}

const char* scopeName(StickyScope s) {
    return s == StickyScope::None ? "none" : (s == StickyScope::Client ? "client" : "group");
}

nlohmann::json toFullResult(nlohmann::json metrics, const BenchmarkConfig& cfg) {
    metrics["experiment"]["name"] = cfg.name;
    metrics["experiment"]["algorithm"] = algoName(cfg.algorithm);
    metrics["experiment"]["profile"] = cfg.profile;

    nlohmann::json c;
    c["duration_ms"] = cfg.duration_ms;
    c["warmup_ms"] = cfg.warmup_ms;
    c["total_request_limit"] = cfg.total_request_limit;
    c["manager_workers"] = cfg.manager_workers;
    c["seed"] = cfg.seed;
    c["servers"] = nlohmann::json::array();
    for (const auto& s : cfg.servers) {
        nlohmann::json sj;
        sj["weight"] = s.weight;
        sj["capacity"] = s.capacity;
        sj["max_parallel_requests"] = s.max_parallel_requests;
        sj["start_at_ms"] = s.start_at_ms;
        sj["crash_at_ms"] = s.crash_at_ms >= 0 ? nlohmann::json(s.crash_at_ms) : nlohmann::json(nullptr);
        c["servers"].push_back(sj);
    }
    c["client_groups"] = nlohmann::json::array();
    for (const auto& g : cfg.client_groups) {
        nlohmann::json gj;
        gj["count"] = g.count;
        gj["sticky_scope"] = scopeName(g.sticky_scope);
        gj["inter_arrival_ms"] = {{"type", "exponential"}, {"center", 1.0}};
        gj["task_cost"] = {{"type", "constant"}, {"value", 0.2}};
        c["client_groups"].push_back(gj);
    }
    metrics["config"] = c;
    return metrics;
}

nlohmann::json runWith(const BenchmarkConfig& base, Algorithm algorithm) {
    BenchmarkConfig cfg = base;
    cfg.algorithm = algorithm;
    asio::io_context io;
    Benchmark bench(io, cfg);
    nlohmann::json metrics = bench.run().toJson();
    return toFullResult(std::move(metrics), cfg);
}

nlohmann::json_schema::json_validator& schemaValidator() {
    static nlohmann::json_schema::json_validator validator(
        [](const nlohmann::json_uri& uri, nlohmann::json& schema) {
            std::string fname = uri.path();
            auto slash = fname.find_last_of('/');
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            std::ifstream ifs(std::string(SCHEMS_DIR) + "/" + fname);
            if (!ifs) throw std::runtime_error("schema not found: " + fname);
            ifs >> schema;
        });
    static bool initialized = [] {
        std::ifstream ifs(std::string(SCHEMS_DIR) + "/result-schema.json");
        nlohmann::json root;
        ifs >> root;
        validator.set_root_schema(root);
        return true;
    }();
    (void)initialized;
    return validator;
}

void expectConformsToSchema(const nlohmann::json& doc) {
    try {
        schemaValidator().validate(doc);
    } catch (const std::exception& e) {
        ADD_FAILURE() << "Нарушение result-schema.json: " << e.what();
    }
}

void expectMatchesSchemaAndInput(const nlohmann::json& j, const BenchmarkConfig& cfg) {
    expectConformsToSchema(j);

    ASSERT_TRUE(j.contains("totals"));
    EXPECT_EQ(j["totals"]["successful"].get<int>() + j["totals"]["failed_final"].get<int>(),
              j["totals"]["requests_sent"].get<int>());
    EXPECT_GE(j["totals"]["requests_sent"].get<int>(), 0);
    EXPECT_GT(j["totals"]["actual_duration_ms"].get<double>(), 0.0);

    ASSERT_TRUE(j.contains("latency"));
    EXPECT_EQ(j["latency"]["unit"], "ms");
    for (const char* p : {"p50", "p95", "p99"}) {
        ASSERT_TRUE(j["latency"]["percentiles"].contains(p)) << p;
    }
    ASSERT_TRUE(j["latency"]["histogram"]["buckets"].is_array());
    EXPECT_EQ(j["latency"]["histogram"]["buckets"].size(), 5u);
    size_t bucket_total = 0;
    for (const auto& b : j["latency"]["histogram"]["buckets"]) bucket_total += b.get<size_t>();
    EXPECT_EQ(bucket_total, j["latency"]["count"].get<size_t>());

    ASSERT_TRUE(j.contains("failures"));
    EXPECT_EQ(j["failures"]["total_final_failures"].get<int>(), j["totals"]["failed_final"].get<int>());
    const auto& by = j["failures"]["by_reason"];
    EXPECT_GE(by["server_crashed"].get<int>(), 0);
    EXPECT_GE(by["server_overloaded"].get<int>(), 0);
    EXPECT_GE(by["timeout"].get<int>(), 0);

    ASSERT_TRUE(j["servers"].is_array());
    ASSERT_EQ(j["servers"].size(), cfg.servers.size());
    for (size_t i = 0; i < cfg.servers.size(); ++i) {
        const auto& srv = j["servers"][i];
        const auto& in = cfg.servers[i];
        EXPECT_EQ(srv["weight"].get<uint32_t>(), in.weight);
        EXPECT_DOUBLE_EQ(srv["capacity"].get<double>(), in.capacity);
        EXPECT_EQ(srv["max_parallel_requests"].get<uint32_t>(), in.max_parallel_requests);
        EXPECT_EQ(srv["start_at_ms"].get<int>(), in.start_at_ms);
        if (in.crash_at_ms > 0) {
            EXPECT_EQ(srv["crash_at_ms"].get<int>(), in.crash_at_ms);
        } else {
            EXPECT_TRUE(srv["crash_at_ms"].is_null());
        }
        EXPECT_GE(srv["max_time_ms"].get<double>(), srv["min_time_ms"].get<double>());
    }
    
    ASSERT_TRUE(j["client_groups"].is_array());
    ASSERT_EQ(j["client_groups"].size(), cfg.client_groups.size());
    const char* scope_names[3] = {"none", "client", "group"};
    int summed_sent = 0;
    for (size_t g = 0; g < cfg.client_groups.size(); ++g) {
        const auto& cg = j["client_groups"][g];
        const auto& in = cfg.client_groups[g];
        EXPECT_EQ(cg["group_index"].get<int>(), static_cast<int>(g));
        EXPECT_EQ(cg["num_clients"].get<int>(), in.count);
        EXPECT_EQ(cg["sticky_scope"].get<std::string>(), scope_names[in.sticky_scope]);
        ASSERT_TRUE(cg["latency"].contains("mean"));
        ASSERT_TRUE(cg["latency"].contains("p95"));
        ASSERT_TRUE(cg["latency"].contains("max"));
        summed_sent += cg["requests_sent"].get<int>();
    }
    EXPECT_EQ(summed_sent, j["totals"]["requests_sent"].get<int>());

    ASSERT_TRUE(j["timeline"].is_array());
    int prev_t = 0;
    for (const auto& e : j["timeline"]) {
        EXPECT_GT(e["t_ms"].get<int>(), prev_t);
        prev_t = e["t_ms"].get<int>();
        EXPECT_GE(e["active_connections"].get<int>(), 0);
    }

    std::string dumped = j.dump();
    nlohmann::json reparsed;
    EXPECT_NO_THROW(reparsed = nlohmann::json::parse(dumped));
    EXPECT_EQ(reparsed["servers"].size(), cfg.servers.size());
}

nlohmann::json inputProjection(const nlohmann::json& j) {
    nlohmann::json out;
    for (const auto& s : j["servers"]) {
        out["servers"].push_back({{"weight", s["weight"]},
                                  {"capacity", s["capacity"]},
                                  {"max_parallel_requests", s["max_parallel_requests"]},
                                  {"start_at_ms", s["start_at_ms"]},
                                  {"crash_at_ms", s["crash_at_ms"]}});
    }
    for (const auto& cg : j["client_groups"]) {
        out["client_groups"].push_back({{"group_index", cg["group_index"]},
                                        {"num_clients", cg["num_clients"]},
                                        {"sticky_scope", cg["sticky_scope"]}});
    }
    return out;
}

}  // namespace

// Каждый тест: один и тот же вход (сид), своя стратегия выбора сервера.
TEST(BenchmarkOutputStress, RoundRobinMatchesSchemaAndInput) {
    auto cfg = heavyConfig(seedFromEnv());
    expectMatchesSchemaAndInput(runWith(cfg, Algorithm::RoundRobin), cfg);
}

TEST(BenchmarkOutputStress, WeightRoundRobinMatchesSchemaAndInput) {
    auto cfg = heavyConfig(seedFromEnv());
    expectMatchesSchemaAndInput(runWith(cfg, Algorithm::WeightRoundRobin), cfg);
}

TEST(BenchmarkOutputStress, LeastConnectionsMatchesSchemaAndInput) {
    auto cfg = heavyConfig(seedFromEnv());
    expectMatchesSchemaAndInput(runWith(cfg, Algorithm::LeastConnections), cfg);
}

TEST(BenchmarkOutputStress, ConsistentHashingMatchesSchemaAndInput) {
    auto cfg = heavyConfig(seedFromEnv());
    expectMatchesSchemaAndInput(runWith(cfg, Algorithm::ConsistentHashing), cfg);
}

// Сравнение всех 4 стратегий на одном входе.
TEST(BenchmarkOutputStress, AllAlgorithmsAgreeOnInputProjection) {
    auto cfg = heavyConfig(seedFromEnv());

    nlohmann::json rr = runWith(cfg, Algorithm::RoundRobin);
    nlohmann::json wrr = runWith(cfg, Algorithm::WeightRoundRobin);
    nlohmann::json lc = runWith(cfg, Algorithm::LeastConnections);
    nlohmann::json ch = runWith(cfg, Algorithm::ConsistentHashing);

    expectMatchesSchemaAndInput(rr, cfg);
    expectMatchesSchemaAndInput(wrr, cfg);
    expectMatchesSchemaAndInput(lc, cfg);
    expectMatchesSchemaAndInput(ch, cfg);

    nlohmann::json base = inputProjection(rr);
    EXPECT_EQ(inputProjection(wrr), base);
    EXPECT_EQ(inputProjection(lc), base);
    EXPECT_EQ(inputProjection(ch), base);

    auto keys = [](const nlohmann::json& j) {
        std::vector<std::string> k;
        for (auto it = j.begin(); it != j.end(); ++it) k.push_back(it.key());
        return k;
    };
    EXPECT_EQ(keys(wrr), keys(rr));
    EXPECT_EQ(keys(lc), keys(rr));
    EXPECT_EQ(keys(ch), keys(rr));
}
