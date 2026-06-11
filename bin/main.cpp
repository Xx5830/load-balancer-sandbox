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
    out["config"] = preset;

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