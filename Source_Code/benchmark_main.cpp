#include "benchmark.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const std::string outputPath = argc > 1 ? argv[1] : "benchmark_results.csv";
    const bool concurrent = argc > 2 && std::string(argv[2]) == "--concurrent";
    const bool smoke = concurrent
        ? (argc > 3 && std::string(argv[3]) == "--smoke")
        : (argc > 2 && std::string(argv[2]) == "--smoke");

    if (concurrent) {
        try {
            BenchmarkRunner runner;
            const ConcurrentBenchmarkConfig config =
                smoke ? ConcurrentBenchmarkConfig::smokeTest()
                      : ConcurrentBenchmarkConfig::defaults();
            const std::vector<ConcurrentBenchmarkResult> results =
                runner.runConcurrent(config);
            std::string errorMessage;
            if (!BenchmarkRunner::writeConcurrentCsv(outputPath, results, errorMessage)) {
                std::cerr << "Could not write concurrent benchmark CSV: "
                          << errorMessage << '\n';
                return 1;
            }
            std::cout << "Wrote " << results.size()
                      << " concurrent benchmark rows to " << outputPath << ".\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Benchmark failed: " << error.what() << '\n';
            return 1;
        }
    }

    BenchmarkConfig config = smoke ? BenchmarkConfig::smokeTest()
                                   : BenchmarkConfig::defaults();
    config.buildMode = BenchmarkRunner::detectedBuildMode();

    try {
        BenchmarkRunner runner(config);
        const std::vector<BenchmarkResult> results = runner.run();
        std::string errorMessage;
        if (!BenchmarkRunner::writeCsv(outputPath, results, errorMessage)) {
            std::cerr << "Could not write benchmark CSV: " << errorMessage << '\n';
            return 1;
        }
        std::cout << "Wrote " << results.size() << " benchmark rows to "
                  << outputPath << " (" << config.buildMode << " build).\n";
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
