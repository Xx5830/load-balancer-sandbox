#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "generators.hpp"

using load_balancer::ConstantGenerator;
using load_balancer::ExponentialGenerator;
using load_balancer::LognormalGenerator;
using load_balancer::NormalGenerator;
using load_balancer::SequenceGenerator;
using load_balancer::UniformGenerator;

namespace {
std::mt19937 makeRng() {
    return std::mt19937(12345);
}
}  // namespace

// ConstantGenerator всегда возвращает одно и то же значение.
TEST(ConstantGenerator, ReturnsFixedValue) {
    ConstantGenerator gen(4.5);
    auto rng = makeRng();

    EXPECT_DOUBLE_EQ(gen.next(rng), 4.5);
    EXPECT_DOUBLE_EQ(gen.next(rng), 4.5);
}

// SequenceGenerator отдаёт элементы по порядку.
TEST(SequenceGenerator, ReturnsValuesInOrder) {
    std::vector<double> v{1.0, 2.0, 3.0};
    SequenceGenerator gen(v);
    auto rng = makeRng();

    EXPECT_DOUBLE_EQ(gen.next(rng), 1.0);
    EXPECT_DOUBLE_EQ(gen.next(rng), 2.0);
    EXPECT_DOUBLE_EQ(gen.next(rng), 3.0);
}

// SequenceGenerator зацикливается после последнего элемента.
TEST(SequenceGenerator, WrapsAroundAfterEnd) {
    std::vector<double> v{10.0, 20.0};
    SequenceGenerator gen(v);
    auto rng = makeRng();

    EXPECT_DOUBLE_EQ(gen.next(rng), 10.0);
    EXPECT_DOUBLE_EQ(gen.next(rng), 20.0);
    EXPECT_DOUBLE_EQ(gen.next(rng), 10.0);
    EXPECT_DOUBLE_EQ(gen.next(rng), 20.0);
}

// UniformGenerator выдаёт значения строго в заданном диапазоне.
TEST(UniformGenerator, StaysWithinRange) {
    UniformGenerator gen(2.0, 5.0);
    auto rng = makeRng();
    for (int i = 0; i < 1000; ++i) {
        double x = gen.next(rng);
        EXPECT_GE(x, 2.0);
        EXPECT_LE(x, 5.0);
    }
}

// NormalGenerator зажимает результат в [min, max].
TEST(NormalGenerator, ClampsToBounds) {
    NormalGenerator gen(0.0, 100.0, 1.0, 3.0);
    auto rng = makeRng();
    for (int i = 0; i < 1000; ++i) {
        double x = gen.next(rng);
        EXPECT_GE(x, 1.0);
        EXPECT_LE(x, 3.0);
    }
}

// ExponentialGenerator выдаёт неотрицательные значения.
TEST(ExponentialGenerator, ProducesNonNegative) {
    ExponentialGenerator gen(2.0);
    auto rng = makeRng();
    for (int i = 0; i < 1000; ++i) {
        EXPECT_GE(gen.next(rng), 0.0);
    }
}

// LognormalGenerator зажимает результат в [min, max].
TEST(LognormalGenerator, ClampsToBounds) {
    LognormalGenerator gen(0.0, 1.0, 0.5, 2.0);
    auto rng = makeRng();
    for (int i = 0; i < 1000; ++i) {
        double x = gen.next(rng);
        EXPECT_GE(x, 0.5);
        EXPECT_LE(x, 2.0);
    }
}
