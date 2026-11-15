#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct BenchmarkResult {
    std::string component;
    std::string operation;
    std::string workload;
    std::size_t inputSize = 0;
    std::size_t vertices = 0;
    std::size_t edges = 0;
    std::size_t repetitions = 0;
    std::size_t operationsPerSample = 0;
    double averageMicroseconds = 0.0;
    double medianMicroseconds = 0.0;
    double minimumMicroseconds = 0.0;
    double maximumMicroseconds = 0.0;
    std::string timeUnit = "microseconds";
    std::uint32_t seed = 0;
    std::string buildMode;
};

struct BenchmarkConfig {
    std::vector<std::size_t> hashSizes;
    std::vector<std::size_t> collisionSizes;
    std::vector<std::size_t> warehouseSizes;
    std::vector<std::size_t> sparseGraphSizes;
    std::vector<std::size_t> denseGraphSizes;
    std::vector<std::size_t> fulfillmentSizes;
    std::size_t repetitions = 7;
    std::uint32_t seed = 20261114;
    std::string buildMode;

    static BenchmarkConfig defaults();
    static BenchmarkConfig smokeTest();
};

struct ConcurrentBenchmarkConfig {
    std::vector<std::size_t> workerCounts{1, 2, 4, 8};
    std::size_t datasetSize = 256;
    std::size_t operationsPerRepetition = 256;
    std::size_t repetitions = 5;
    std::size_t warmupOperations = 32;

    static ConcurrentBenchmarkConfig defaults();
    static ConcurrentBenchmarkConfig smokeTest();
};

struct ConcurrentBenchmarkResult {
    std::string workloadType;
    std::size_t workerCount = 0;
    std::size_t datasetSize = 0;
    // Completed operations; attempts equal this value plus failedOperations.
    std::size_t totalOperations = 0;
    std::size_t repetitions = 0;
    double averageLatencyMicroseconds = 0.0;
    double p50LatencyMicroseconds = 0.0;
    double p95LatencyMicroseconds = 0.0;
    double p99LatencyMicroseconds = 0.0;
    double throughputOperationsPerSecond = 0.0;
    double totalElapsedMilliseconds = 0.0;
    std::size_t failedOperations = 0;
};

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(BenchmarkConfig config = BenchmarkConfig::defaults());

    std::vector<BenchmarkResult> run() const;
    std::vector<ConcurrentBenchmarkResult> runConcurrent(
        const ConcurrentBenchmarkConfig& concurrentConfig =
            ConcurrentBenchmarkConfig::defaults()) const;
    static bool writeCsv(const std::string& path,
                         const std::vector<BenchmarkResult>& results,
                         std::string& errorMessage);
    static bool writeConcurrentCsv(
        const std::string& path,
        const std::vector<ConcurrentBenchmarkResult>& results,
        std::string& errorMessage);
    static std::string detectedBuildMode();

private:
    BenchmarkConfig config;
};

#endif
