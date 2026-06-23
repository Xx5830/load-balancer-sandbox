#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace load_balancer {

enum class GeneratorType { SEQUENCE, UNIFORM, NORMAL, EXPONENTIAL, LOGNORMAL };

struct IGenerator {
    virtual double next(std::mt19937& rnd) = 0;
    virtual ~IGenerator() = default;
};

class ConstantGenerator : public IGenerator {
   public:
    explicit ConstantGenerator(double value)
        : value_(value) {}

    double next(std::mt19937&) override {
        return value_;
    }

   private:
    double value_;
};

struct SequenceGenerator : public IGenerator {
   private:
    std::vector<double> seq_;
    size_t pos_;
    std::mutex mtx_;

   public:
    SequenceGenerator(std::span<double> seq) {
        pos_ = 0;
        seq_.resize(seq.size());
        for (size_t index = 0; index < seq.size(); index++) {
            seq_[index] = seq[index];
        }
    }
    double next(std::mt19937&) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (pos_ >= seq_.size()) {
            pos_ = 0;
        }
        return seq_[pos_++];
    }
};

class UniformGenerator : public IGenerator {
   public:
    UniformGenerator(double min, double max)
        : dist_(min, max) {}
    double next(std::mt19937& rng) override {
        std::lock_guard<std::mutex> lock(mtx_);
        return dist_(rng);
    }

   private:
    std::uniform_real_distribution<double> dist_;
    std::mutex mtx_;
};

class NormalGenerator : public IGenerator {
   public:
    NormalGenerator(double center, double deviation, std::optional<double> min = {}, std::optional<double> max = {})
        : dist_(center, deviation)
        , min_(min)
        , max_(max) {}

    double next(std::mt19937& rng) override {
        std::lock_guard<std::mutex> lock(mtx_);
        double val = dist_(rng);
        if (min_) {
            val = std::max(*min_, val);
        }
        if (max_) {
            val = std::min(*max_, val);
        }
        return val;
    }

   private:
    std::normal_distribution<double> dist_;
    std::optional<double> min_, max_;
    std::mutex mtx_;
};

class ExponentialGenerator : public IGenerator {
   public:
    explicit ExponentialGenerator(double mean)
        : dist_(1.0 / mean) {}
    double next(std::mt19937& rng) override {
        std::lock_guard<std::mutex> lock(mtx_);
        return dist_(rng);
    }

   private:
    std::exponential_distribution<double> dist_;
    std::mutex mtx_;
};

class LognormalGenerator : public IGenerator {
   public:
    LognormalGenerator(double center, double deviation, std::optional<double> min = {}, std::optional<double> max = {})
        : dist_(center, deviation)
        , min_(min)
        , max_(max) {}
    double next(std::mt19937& rng) override {
        std::lock_guard<std::mutex> lock(mtx_);
        double val = dist_(rng);
        if (min_) {
            val = std::max(*min_, val);
        }
        if (max_) {
            val = std::min(*max_, val);
        }
        return val;
    }

   private:
    std::lognormal_distribution<double> dist_;
    std::optional<double> min_, max_;
    std::mutex mtx_;
};

using GeneratorPtr = std::shared_ptr<IGenerator>;

}  // namespace load_balancer