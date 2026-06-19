#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <random>

#include "preset-parser.hpp"

using load_balancer::Algorithm;
using load_balancer::parseConfig;
using load_balancer::parseGenerator;
using load_balancer::StickyScope;
using nlohmann::json;

namespace {
json minimalPreset() {
    return json{
        {"servers", json::array({{{"weight", 2}}})},
        {"client_groups",
         json::array({{{"count", 1},
                       {"inter_arrival_ms", json::array({1.0})},
                       {"task_cost", json::array({1.0})}}})},
    };
}
}  // namespace

// Массив трактуется как SequenceGenerator.
TEST(ParseGenerator, ArrayBecomesSequence) {
    auto gen = parseGenerator(json::array({1.0, 2.0}));
    ASSERT_NE(gen, nullptr);
    std::mt19937 rng(1);
    EXPECT_DOUBLE_EQ(gen->next(rng), 1.0);
}

// uniform-генератор отдаёт значения в диапазоне.
TEST(ParseGenerator, UniformWithinRange) {
    auto gen = parseGenerator(json{{"type", "uniform"}, {"min", 1.0}, {"max", 2.0}});
    ASSERT_NE(gen, nullptr);
    std::mt19937 rng(1);
    double v = gen->next(rng);
    EXPECT_GE(v, 1.0);
    EXPECT_LE(v, 2.0);
}

// constant-генератор поддерживает формат из схемы и web-формы.
TEST(ParseGenerator, ConstantReturnsValue) {
    auto gen = parseGenerator(json{{"type", "constant"}, {"value", 0.35}});
    ASSERT_NE(gen, nullptr);
    std::mt19937 rng(1);
    EXPECT_DOUBLE_EQ(gen->next(rng), 0.35);
}

// Число трактуется как constant-генератор для полей generator_or_number.
TEST(ParseGenerator, NumberBecomesConstant) {
    auto gen = parseGenerator(json(0.25));
    ASSERT_NE(gen, nullptr);
    std::mt19937 rng(1);
    EXPECT_DOUBLE_EQ(gen->next(rng), 0.25);
}

// Неизвестный тип генератора бросает исключение.
TEST(ParseGenerator, UnknownTypeThrows) {
    EXPECT_THROW(parseGenerator(json{{"type", "no_such"}}), std::runtime_error);
}

// Минимальный пресет парсится с дефолтами.
TEST(ParseConfig, MinimalPresetDefaults) {
    auto cfg = parseConfig(minimalPreset());
    EXPECT_EQ(cfg.name, "experiment");
    EXPECT_EQ(cfg.algorithm, Algorithm::RoundRobin);
    EXPECT_EQ(cfg.duration_ms, 30000);
    EXPECT_EQ(cfg.seed, 42);
    ASSERT_EQ(cfg.servers.size(), 1u);
    EXPECT_EQ(cfg.servers[0].weight, 2u);
    EXPECT_DOUBLE_EQ(cfg.servers[0].capacity, 1.0);
    ASSERT_NE(cfg.servers[0].background_load_gen, nullptr);
    ASSERT_EQ(cfg.client_groups.size(), 1u);
    EXPECT_EQ(cfg.client_groups[0].count, 1);
    EXPECT_EQ(cfg.client_groups[0].sticky_scope, StickyScope::None);
}

// Имя алгоритма из пресета корректно отображается в enum.
TEST(ParseConfig, AlgorithmIsParsed) {
    auto preset = minimalPreset();
    preset["algorithm"] = "LeastConnections";
    EXPECT_EQ(parseConfig(preset).algorithm, Algorithm::LeastConnections);
}

// Неизвестный алгоритм бросает исключение.
TEST(ParseConfig, UnknownAlgorithmThrows) {
    auto preset = minimalPreset();
    preset["algorithm"] = "Magic";
    EXPECT_THROW(parseConfig(preset), std::runtime_error);
}

// Отсутствие servers — ошибка.
TEST(ParseConfig, MissingServersThrows) {
    auto preset = minimalPreset();
    preset.erase("servers");
    EXPECT_THROW(parseConfig(preset), std::runtime_error);
}

// Отсутствие client_groups — ошибка.
TEST(ParseConfig, MissingClientGroupsThrows) {
    auto preset = minimalPreset();
    preset.erase("client_groups");
    EXPECT_THROW(parseConfig(preset), std::runtime_error);
}

// Неизвестный sticky_scope бросает исключение.
TEST(ParseConfig, UnknownStickyScopeThrows) {
    auto preset = minimalPreset();
    preset["client_groups"][0]["sticky_scope"] = "bogus";
    EXPECT_THROW(parseConfig(preset), std::runtime_error);
}

// background_load из server_config доходит до BenchmarkConfig.
TEST(ParseConfig, ServerBackgroundLoadIsParsed) {
    auto preset = minimalPreset();
    preset["servers"][0]["background_load"] = json{{"type", "constant"}, {"value", 0.6}};

    auto cfg = parseConfig(preset);
    ASSERT_EQ(cfg.servers.size(), 1u);
    ASSERT_NE(cfg.servers[0].background_load_gen, nullptr);

    std::mt19937 rng(1);
    EXPECT_DOUBLE_EQ(cfg.servers[0].background_load_gen->next(rng), 0.6);
}

// server_model_params переопределяются из пресета.
TEST(ParseConfig, ServerModelParamsOverridden) {
    auto preset = minimalPreset();
    preset["server_model_params"] = json{{"reject_threshold_seconds", 9.0}};
    auto cfg = parseConfig(preset);
    EXPECT_DOUBLE_EQ(cfg.server_model_params.reject_threshold_seconds_, 9.0);
}
