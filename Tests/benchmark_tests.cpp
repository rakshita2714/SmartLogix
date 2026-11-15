#include "benchmark.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cout << "[FAIL] " << message << '\n';
    }
}

void report(const std::string& name, int before) {
    if (failures == before) std::cout << "[PASS] " << name << '\n';
}

const BenchmarkResult* findResult(const std::vector<BenchmarkResult>& rows,
                                  const std::string& component,
                                  const std::string& operation,
                                  const std::string& workload) {
    for (const BenchmarkResult& row : rows)
        if (row.component == component && row.operation == operation && row.workload == workload)
            return &row;
    return nullptr;
}
} // namespace

int main() {
    BenchmarkConfig config = BenchmarkConfig::smokeTest();
    config.seed = 12345;
    config.buildMode = "test optimized";
    BenchmarkRunner runner(config);
    std::vector<BenchmarkResult> rows;
    try {
        rows = runner.run();
    } catch (const std::exception& error) {
        std::cout << "[FAIL] Smoke runner threw: " << error.what() << '\n';
        return 1;
    }

    int before = failures;
    check(!rows.empty(), "smoke runner produces results");
    check(rows.size() == 44, "smoke run emits every configured operation and workload");
    report("Smoke configuration produces benchmark rows", before);

    before = failures;
    std::set<std::string> categories;
    for (const BenchmarkResult& row : rows) {
        categories.insert(row.component);
        check(row.inputSize > 0, "benchmark input size is positive");
        check(row.repetitions == config.repetitions, "repetition metadata matches config");
        const bool zeroWorkload = row.component == "FulfillmentGraph" &&
                                  row.operation == "dependency insertion" &&
                                  row.workload == "independent";
        check(zeroWorkload ? row.operationsPerSample == 0 : row.operationsPerSample > 0,
              "operation batch metadata matches the workload");
        check(row.seed == config.seed, "seed metadata is preserved");
        check(row.buildMode == config.buildMode, "build mode metadata is preserved");
        check(row.timeUnit == "microseconds", "time unit is microseconds");
        check(std::isfinite(row.averageMicroseconds) && row.averageMicroseconds >= 0.0,
              "average timing is finite and non-negative");
        check(std::isfinite(row.medianMicroseconds) && row.medianMicroseconds >= 0.0,
              "median timing is finite and non-negative");
        check(std::isfinite(row.minimumMicroseconds) && row.minimumMicroseconds >= 0.0,
              "minimum timing is finite and non-negative");
        check(std::isfinite(row.maximumMicroseconds) && row.maximumMicroseconds >= 0.0,
              "maximum timing is finite and non-negative");
        check(row.minimumMicroseconds <= row.medianMicroseconds &&
              row.medianMicroseconds <= row.maximumMicroseconds, "timing statistics are ordered");
    }
    check(categories.size() == 4, "all four benchmark components are represented");
    report("Result metadata and timing values are valid", before);

    before = failures;
    for (const std::string& item : {
             "HashTable|insertion|uniform", "HashTable|automatic resizing|uniform",
             "HashTable|collision search first|single_bucket_chain",
             "HashTable|collision search last|single_bucket_chain",
             "HashTable|collision search missing|single_bucket_chain",
             "WarehouseManager|bulk insertion|uniform", "WarehouseManager|successful search|uniform",
             "WarehouseManager|unsuccessful search|uniform", "WarehouseManager|batch update|uniform",
             "WarehouseManager|delete first|uniform", "WarehouseManager|delete last|uniform",
             "TransportationGraph|graph construction|sparse",
             "TransportationGraph|graph construction|dense",
             "TransportationGraph|graph construction|disconnected",
             "TransportationGraph|Dijkstra|sparse", "TransportationGraph|BFS|disconnected",
             "TransportationGraph|DFS|dense", "FulfillmentGraph|operation insertion|independent",
             "FulfillmentGraph|dependency insertion|chain", "FulfillmentGraph|cycle detection|cyclic",
             "FulfillmentGraph|topological sort|branching"}) {
        std::istringstream parts(item);
        std::string component, operation, workload;
        std::getline(parts, component, '|');
        std::getline(parts, operation, '|');
        std::getline(parts, workload, '|');
        check(findResult(rows, component, operation, workload) != nullptr,
              "configured workload exists: " + item);
    }
    report("All approved benchmark categories and workload families are present", before);

    before = failures;
    const BenchmarkResult* disconnected = findResult(rows, "TransportationGraph", "BFS", "disconnected");
    const BenchmarkResult* dense = findResult(rows, "TransportationGraph", "graph construction", "dense");
    check(disconnected != nullptr && disconnected->vertices == 30 && disconnected->edges == 28,
          "disconnected generator has two known components");
    check(dense != nullptr && dense->vertices == 20 && dense->edges > 19,
          "dense generator contains more than its spanning backbone");
    const BenchmarkResult* cyclic = findResult(rows, "FulfillmentGraph", "cycle detection", "cyclic");
    check(cyclic != nullptr && cyclic->edges == 100,
          "cyclic fulfillment workload includes closing edge");
    report("Graph workload metadata describes intended properties", before);

    before = failures;
    const std::string outputPath = "benchmark_test_output.csv";
    std::string error;
    check(BenchmarkRunner::writeCsv(outputPath, rows, error), "CSV export succeeds: " + error);
    std::ifstream input(outputPath.c_str());
    std::string header;
    std::getline(input, header);
    check(header == "component,operation,workload,input_size,vertices,edges,repetitions,operations_per_sample,avg_us,median_us,min_us,max_us,time_unit,seed,build_mode",
          "CSV header matches the required schema");
    std::size_t lineCount = 0;
    std::string line;
    while (std::getline(input, line)) ++lineCount;
    check(lineCount == rows.size(), "CSV contains one row per result");
    input.close();
    std::remove(outputPath.c_str());
    report("CSV output has the exact header and complete result rows", before);

    before = failures;
    BenchmarkResult escaped;
    escaped.component = "Hash, Table";
    escaped.operation = "search \"quoted\"";
    escaped.workload = "line\nbreak";
    escaped.inputSize = escaped.repetitions = escaped.operationsPerSample = 1;
    escaped.averageMicroseconds = escaped.medianMicroseconds =
        escaped.minimumMicroseconds = escaped.maximumMicroseconds = 0.5;
    escaped.seed = config.seed;
    escaped.buildMode = config.buildMode;
    const std::string escapedPath = "benchmark_escape_test.csv";
    check(BenchmarkRunner::writeCsv(escapedPath, {escaped}, error), "CSV escaping export succeeds");
    std::ifstream escapedFile(escapedPath.c_str());
    std::getline(escapedFile, header);
    std::getline(escapedFile, line);
    check(line.find("\"Hash, Table\"") == 0, "CSV quotes comma-containing fields");
    check(line.find("\"search \"\"quoted\"\"\"") != std::string::npos,
          "CSV doubles embedded quote characters");
    escapedFile.close();
    std::remove(escapedPath.c_str());
    check(!BenchmarkRunner::writeCsv("missing_test_directory/results.csv", rows, error),
          "CSV open failure is reported");
    BenchmarkResult invalid = escaped;
    invalid.averageMicroseconds = std::numeric_limits<double>::infinity();
    check(!BenchmarkRunner::writeCsv("benchmark_invalid_test.csv", {invalid}, error),
          "non-finite result timing is rejected");
    std::remove("benchmark_invalid_test.csv");
    report("CSV escaping and output failures are handled", before);

    before = failures;
    const ConcurrentBenchmarkConfig concurrentConfig =
        ConcurrentBenchmarkConfig::smokeTest();
    std::vector<ConcurrentBenchmarkResult> concurrentRows;
    try {
        concurrentRows = runner.runConcurrent(concurrentConfig);
    } catch (const std::exception& exception) {
        check(false, std::string("concurrent benchmark throws: ") + exception.what());
    }
    check(concurrentRows.size() == 12,
          "concurrent benchmark covers all workloads and worker counts");
    std::set<std::string> concurrentWorkloads;
    std::set<std::size_t> concurrentWorkers;
    bool metricsValid = true;
    for (const ConcurrentBenchmarkResult& result : concurrentRows) {
        concurrentWorkloads.insert(result.workloadType);
        concurrentWorkers.insert(result.workerCount);
        const std::size_t expectedOperations =
            concurrentConfig.operationsPerRepetition * concurrentConfig.repetitions;
        const double expectedThroughput =
            static_cast<double>(result.totalOperations) /
            (result.totalElapsedMilliseconds / 1000.0);
        metricsValid = metricsValid && result.datasetSize == concurrentConfig.datasetSize &&
            result.repetitions == concurrentConfig.repetitions &&
            result.totalOperations + result.failedOperations == expectedOperations &&
            result.failedOperations == 0 &&
            std::isfinite(result.averageLatencyMicroseconds) &&
            result.averageLatencyMicroseconds >= 0.0 &&
            result.p50LatencyMicroseconds <= result.p95LatencyMicroseconds &&
            result.p95LatencyMicroseconds <= result.p99LatencyMicroseconds &&
            std::isfinite(result.throughputOperationsPerSecond) &&
            std::abs(result.throughputOperationsPerSecond - expectedThroughput) <
                std::max(1e-9, expectedThroughput * 1e-9) &&
            std::isfinite(result.totalElapsedMilliseconds) &&
            result.totalElapsedMilliseconds > 0.0;
    }
    check(metricsValid && concurrentWorkloads.size() == 3 &&
              concurrentWorkers == std::set<std::size_t>({1, 2, 4, 8}),
          "concurrent metrics, worker counts, and deterministic requests are valid");
    report("Concurrent benchmark workloads and metrics are valid", before);

    before = failures;
    const std::string concurrentPath = "concurrent_benchmark_test_output.csv";
    check(BenchmarkRunner::writeConcurrentCsv(concurrentPath, concurrentRows, error),
          "concurrent CSV export succeeds: " + error);
    std::ifstream concurrentFile(concurrentPath);
    std::string concurrentHeader;
    std::getline(concurrentFile, concurrentHeader);
    check(concurrentHeader ==
              "workload_type,worker_count,dataset_size,total_operations,repetitions,average_latency_us,p50_latency_us,p95_latency_us,p99_latency_us,throughput_operations_per_second,total_elapsed_ms,failed_operations",
          "concurrent CSV header matches the required metric schema");
    std::size_t concurrentLineCount = 0;
    std::string concurrentLine;
    while (std::getline(concurrentFile, concurrentLine)) ++concurrentLineCount;
    concurrentFile.close();
    check(concurrentLineCount == concurrentRows.size(),
          "concurrent CSV contains one row per benchmark result");
    std::remove(concurrentPath.c_str());
    report("Concurrent CSV output has the required columns and rows", before);

    if (failures == 0) std::cout << "7/7 test groups passed\n";
    else std::cout << failures << " assertion(s) failed\n";
    return failures == 0 ? 0 : 1;
}
