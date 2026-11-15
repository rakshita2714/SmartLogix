#include "benchmark.h"

#include "algorithms.h"
#include "fulfillment.h"
#include "graph.h"
#include "hash_table.h"
#include "smartlogix_service.h"
#include "thread_pool.h"
#include "warehouse_manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;
using EdgeSpec = std::pair<std::size_t, std::size_t>;
volatile std::size_t gConsumedSize = 0;
volatile double gConsumedDistance = 0.0;

std::string numberedId(const char* prefix, std::size_t number, std::size_t width = 6) {
    std::ostringstream output;
    output << prefix << std::setw(static_cast<int>(width)) << std::setfill('0') << number;
    return output.str();
}

std::vector<std::string> makeIds(const char* prefix, std::size_t count) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (std::size_t i = 0; i < count; ++i) ids.push_back(numberedId(prefix, i));
    return ids;
}

std::vector<Warehouse> makeWarehouses(const std::vector<std::string>& ids) {
    std::vector<Warehouse> warehouses;
    warehouses.reserve(ids.size());
    for (const std::string& id : ids) warehouses.push_back({id, "Benchmark Depot", "Test", 1000});
    return warehouses;
}

void populateWarehouseManager(WarehouseManager& manager,
                              const std::vector<Warehouse>& warehouses) {
    for (const Warehouse& warehouse : warehouses) {
        if (!manager.addWarehouse(warehouse)) throw std::runtime_error("warehouse setup failed");
    }
}

double elapsedMicroseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

void populateConcurrentService(SmartLogixService& service, std::size_t datasetSize) {
    for (std::size_t i = 0; i < datasetSize; ++i) {
        const std::string id = numberedId("CWH", i);
        if (!service.addWarehouse({id, "Concurrent Depot", "Benchmark", 1000})) {
            throw std::runtime_error("concurrent benchmark warehouse setup failed");
        }
        if (!service.addOperation(numberedId("COP", i))) {
            throw std::runtime_error("concurrent benchmark operation setup failed");
        }
    }
    for (std::size_t i = 0; i < datasetSize; ++i) {
        const std::string from = numberedId("CWH", i);
        const std::string to = numberedId("CWH", (i + 1) % datasetSize);
        if (!service.addRoute(from, to, 1.0)) {
            throw std::runtime_error("concurrent benchmark route setup failed");
        }
        const std::string chord = numberedId("CWH", (i + 2) % datasetSize);
        if (!service.addRoute(from, chord, 2.0)) {
            throw std::runtime_error("concurrent benchmark chord setup failed");
        }
        if (i != 0 && !service.addDependency(numberedId("COP", i - 1),
                                             numberedId("COP", i))) {
            throw std::runtime_error("concurrent benchmark dependency setup failed");
        }
    }
}

bool performConcurrentOperation(SmartLogixService& service,
                                const std::string& workload,
                                std::size_t operationIndex,
                                std::size_t datasetSize) {
    const std::size_t warehouseIndex = operationIndex % datasetSize;
    const std::string warehouseId = numberedId("CWH", warehouseIndex);

    if (workload == "read-heavy") {
        switch (operationIndex % 7) {
        case 0:
            return service.searchWarehouse(warehouseId).has_value();
        case 1: {
            const ShortestPathResult result = service.shortestPath(
                warehouseId, numberedId("CWH", (warehouseIndex + 1) % datasetSize));
            return result.status == ShortestPathStatus::Found;
        }
        case 2:
            return service.breadthFirstSearch(warehouseId).validStart;
        case 3:
            return service.depthFirstSearch(warehouseId).validStart;
        case 4:
            return !service.hasFulfillmentCycle();
        case 5:
            return service.topologicalSort().status == TopologicalSortStatus::Success;
        default:
            return service.getOperationIds().size() == datasetSize;
        }
    }

    if (workload == "write-heavy") {
        switch (operationIndex % 3) {
        case 0:
            return service.addWarehouse({numberedId("ADDWH", operationIndex),
                                         "Added Depot", "Benchmark", 500});
        case 1: {
            const std::size_t edgeIndex = operationIndex / 3;
            const std::string from = numberedId("CWH", edgeIndex % datasetSize);
            const std::string to = numberedId("CWH", (edgeIndex + 2) % datasetSize);
            return service.removeRoute(from, to) && service.addRoute(from, to, 2.0);
        }
        default:
            return service.addOperation(numberedId("ADDOP", operationIndex));
        }
    }

    switch (operationIndex % 6) {
    case 0:
        return service.searchWarehouse(warehouseId).has_value();
    case 1: {
        const ShortestPathResult result = service.shortestPath(
            warehouseId, numberedId("CWH", (warehouseIndex + 1) % datasetSize));
        return result.status == ShortestPathStatus::Found;
    }
    case 2:
        return service.addWarehouse({numberedId("MIXWH", operationIndex),
                                     "Mixed Depot", "Benchmark", 500});
    case 3: {
        const std::size_t edgeIndex = operationIndex / 6;
        const std::string from = numberedId("CWH", edgeIndex % datasetSize);
        const std::string to = numberedId("CWH", (edgeIndex + 2) % datasetSize);
        return service.removeRoute(from, to) && service.addRoute(from, to, 2.0);
    }
    case 4:
        return service.topologicalSort().status == TopologicalSortStatus::Success;
    default:
        return service.addOperation(numberedId("MIXOP", operationIndex));
    }
}

struct ConcurrentTaskSample {
    bool success;
    double latencyMicroseconds;
};

struct ConcurrentBatchSample {
    std::vector<double> latencies;
    std::size_t completedOperations = 0;
    std::size_t failedOperations = 0;
    double elapsedMilliseconds = 0.0;
};

ConcurrentBatchSample runConcurrentBatch(const std::string& workload,
                                         std::size_t workerCount,
                                         std::size_t datasetSize,
                                         std::size_t operationCount) {
    SmartLogixService service;
    populateConcurrentService(service, datasetSize);
    ThreadPool pool(workerCount, operationCount);

    std::vector<std::future<ConcurrentTaskSample>> futures;
    futures.reserve(operationCount);
    ConcurrentBatchSample sample;
    sample.latencies.reserve(operationCount);

    const Clock::time_point batchStart = Clock::now();
    for (std::size_t i = 0; i < operationCount; ++i) {
        const Clock::time_point submittedAt = Clock::now();
        try {
            futures.push_back(pool.submit([&service, workload, datasetSize, i, submittedAt]() {
                bool success = false;
                try {
                    success = performConcurrentOperation(service, workload, i, datasetSize);
                } catch (...) {
                    success = false;
                }
                return ConcurrentTaskSample{success, elapsedMicroseconds(submittedAt)};
            }));
        } catch (...) {
            ++sample.failedOperations;
            sample.latencies.push_back(elapsedMicroseconds(submittedAt));
        }
    }

    for (std::future<ConcurrentTaskSample>& future : futures) {
        try {
            const ConcurrentTaskSample task = future.get();
            sample.latencies.push_back(task.latencyMicroseconds);
            if (task.success) {
                ++sample.completedOperations;
            } else {
                ++sample.failedOperations;
            }
        } catch (...) {
            ++sample.failedOperations;
            sample.latencies.push_back(0.0);
        }
    }
    sample.elapsedMilliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - batchStart).count();
    pool.shutdown();
    return sample;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

template <typename Warmup, typename Sample>
BenchmarkResult measure(const BenchmarkConfig& config,
                        const std::string& component,
                        const std::string& operation,
                        const std::string& workload,
                        std::size_t inputSize,
                        std::size_t vertices,
                        std::size_t edges,
                        std::size_t operationsPerSample,
                        Warmup warmup,
                        Sample sample) {
    warmup(); // Warm-up uses a distinct structure from every measured repetition.
    std::vector<double> samples;
    samples.reserve(config.repetitions);
    for (std::size_t repetition = 0; repetition < config.repetitions; ++repetition) {
        samples.push_back(sample());
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.size() % 2 == 0
        ? (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0
        : sorted[sorted.size() / 2];
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {component, operation, workload, inputSize, vertices, edges,
            config.repetitions, operationsPerSample,
            total / static_cast<double>(samples.size()), median,
            sorted.front(), sorted.back(), "microseconds", config.seed,
            config.buildMode.empty() ? BenchmarkRunner::detectedBuildMode() : config.buildMode};
}

std::uint64_t bucketHash(const std::string& key, std::size_t capacity) {
    std::uint64_t hash = 0;
    for (unsigned char ch : key) hash = (hash * 31 + ch) % capacity;
    return hash;
}

std::vector<std::string> makeCollisionIds(std::size_t count, std::size_t capacity) {
    std::vector<std::string> ids;
    ids.reserve(count + 1);
    std::string missing;
    for (std::size_t i = 0; ids.size() <= count; ++i) {
        const std::string candidate = numberedId("COL", i);
        if (bucketHash(candidate, capacity) == 7) {
            if (ids.size() < count) ids.push_back(candidate);
            else {
                missing = candidate;
                break;
            }
        }
    }
    ids.push_back(missing);
    return ids;
}

struct NetworkSpec {
    std::string workload;
    std::size_t vertices;
    std::vector<EdgeSpec> edges;
    std::size_t reachableFromZero;
};

NetworkSpec makeNetwork(const std::string& workload,
                        std::size_t count,
                        std::uint32_t seed) {
    NetworkSpec spec{workload, count, {}, count};
    if (count == 0) return spec;
    std::set<EdgeSpec> seen;
    auto add = [&spec, &seen](std::size_t from, std::size_t to) {
        if (from == to) return;
        if (!seen.emplace(from, to).second) return;
        spec.edges.push_back({from, to});
    };

    if (workload == "disconnected") {
        const std::size_t split = (count + 1) / 2;
        spec.reachableFromZero = split;
        for (std::size_t i = 1; i < split; ++i) add(i - 1, i);
        for (std::size_t i = split + 1; i < count; ++i) add(i - 1, i);
        return spec;
    }

    // A cheap forward backbone gives a known shortest path and makes sparse
    // workloads reachable. Extra edges are deliberately more expensive.
    for (std::size_t i = 1; i < count; ++i) add(i - 1, i);
    if (workload == "dense") {
        const std::size_t degree = std::min<std::size_t>(40, count > 0 ? count - 1 : 0);
        for (std::size_t from = 0; from < count; ++from)
            for (std::size_t offset = 2; offset <= degree; ++offset)
                if (from + offset < count) add(from, from + offset);
    } else {
        std::mt19937 generator(seed ^ static_cast<std::uint32_t>(count));
        const std::size_t extraCount = count * 2;
        for (std::size_t i = 0, attempts = 0; i < extraCount && attempts < extraCount * 8 + 1; ++attempts) {
            const std::size_t from = generator() % count;
            const std::size_t to = generator() % count;
            if (from != to && to > from && to - from > 1) {
                const std::size_t before = spec.edges.size();
                add(from, to);
                if (spec.edges.size() != before) ++i;
            }
        }
    }
    return spec;
}

// Keep the manager alive while the graph refers to it; all mutation happens in this scope.
template <typename Callback>
void withNetworkData(const NetworkSpec& spec,
                     const std::vector<std::string>& ids,
                     const std::vector<Warehouse>& warehouses,
                     Callback callback) {
    WarehouseManager manager;
    populateWarehouseManager(manager, warehouses);
    TransportationGraph graph(manager);
    for (const std::string& id : ids)
        if (!graph.registerWarehouseVertex(id)) throw std::runtime_error("vertex setup failed");
    for (const EdgeSpec& edge : spec.edges) {
        const double distance = edge.second == edge.first + 1
            ? 1.0 : static_cast<double>(spec.vertices + 1);
        if (!graph.addRoute(ids[edge.first], ids[edge.second], distance))
            throw std::runtime_error("route setup failed");
    }
    callback(manager, graph, ids);
}

template <typename Callback>
void withNetwork(const NetworkSpec& spec, Callback callback) {
    const std::vector<std::string> ids = makeIds("WH", spec.vertices);
    const std::vector<Warehouse> warehouses = makeWarehouses(ids);
    withNetworkData(spec, ids, warehouses, callback);
}

std::vector<EdgeSpec> fulfillmentEdges(const std::string& workload, std::size_t count) {
    std::vector<EdgeSpec> edges;
    if (workload == "independent") return edges;
    for (std::size_t i = 1; i < count; ++i) {
        const std::size_t prerequisite = workload == "branching" ? (i - 1) / 2 : i - 1;
        edges.push_back({prerequisite, i});
    }
    if (workload == "cyclic" && count > 1) edges.push_back({count - 1, 0});
    return edges;
}

bool setupFulfillment(FulfillmentGraph& graph,
                      const std::vector<std::string>& ids,
                      const std::vector<EdgeSpec>& edges) {
    for (const std::string& id : ids) if (!graph.addOperation(id)) return false;
    for (const EdgeSpec& edge : edges)
        if (!graph.addDependency(ids[edge.first], ids[edge.second])) return false;
    return true;
}

std::string escapeCsv(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') escaped += "\"\"";
        else escaped += ch;
    }
    escaped += '"';
    return escaped;
}

void writeRow(std::ostream& output, const BenchmarkResult& result) {
    output << escapeCsv(result.component) << ',' << escapeCsv(result.operation) << ','
           << escapeCsv(result.workload) << ',' << result.inputSize << ','
           << result.vertices << ',' << result.edges << ',' << result.repetitions << ','
           << result.operationsPerSample << ',' << std::setprecision(12)
           << result.averageMicroseconds << ',' << result.medianMicroseconds << ','
           << result.minimumMicroseconds << ',' << result.maximumMicroseconds << ','
           << escapeCsv(result.timeUnit) << ',' << result.seed << ','
           << escapeCsv(result.buildMode) << '\n';
}
} // namespace

BenchmarkConfig BenchmarkConfig::defaults() {
    BenchmarkConfig config;
    config.hashSizes = {1000, 5000, 10000, 25000};
    config.collisionSizes = {100, 250, 500, 1000};
    config.warehouseSizes = {1000, 5000, 10000, 25000};
    config.sparseGraphSizes = {100, 250, 500, 1000, 2000};
    config.denseGraphSizes = {50, 100, 200, 400};
    config.fulfillmentSizes = {1000, 5000, 10000, 25000};
    config.repetitions = 7;
    return config;
}

BenchmarkConfig BenchmarkConfig::smokeTest() {
    BenchmarkConfig config;
    config.hashSizes = {100};
    config.collisionSizes = {25};
    config.warehouseSizes = {100};
    config.sparseGraphSizes = {30};
    config.denseGraphSizes = {20};
    config.fulfillmentSizes = {100};
    config.repetitions = 2;
    return config;
}

ConcurrentBenchmarkConfig ConcurrentBenchmarkConfig::defaults() {
    ConcurrentBenchmarkConfig config;
    config.workerCounts = {1, 2, 4, 8};
    config.datasetSize = 256;
    config.operationsPerRepetition = 256;
    config.repetitions = 5;
    config.warmupOperations = 32;
    return config;
}

ConcurrentBenchmarkConfig ConcurrentBenchmarkConfig::smokeTest() {
    ConcurrentBenchmarkConfig config;
    config.workerCounts = {1, 2, 4, 8};
    config.datasetSize = 32;
    config.operationsPerRepetition = 48;
    config.repetitions = 2;
    config.warmupOperations = 8;
    return config;
}

BenchmarkRunner::BenchmarkRunner(BenchmarkConfig supplied) : config(std::move(supplied)) {
    if (config.repetitions == 0) throw std::invalid_argument("repetitions must be positive");
    const auto containsZero = [](const std::vector<std::size_t>& sizes) {
        return std::find(sizes.begin(), sizes.end(), 0) != sizes.end();
    };
    if (containsZero(config.hashSizes) || containsZero(config.collisionSizes) ||
        containsZero(config.warehouseSizes) || containsZero(config.sparseGraphSizes) ||
        containsZero(config.denseGraphSizes) || containsZero(config.fulfillmentSizes))
        throw std::invalid_argument("workload sizes must be positive");
    if (config.buildMode.empty()) config.buildMode = detectedBuildMode();
}

std::string BenchmarkRunner::detectedBuildMode() {
#if defined(__OPTIMIZE__)
    return "optimized";
#else
    return "unoptimized";
#endif
}

std::vector<BenchmarkResult> BenchmarkRunner::run() const {
    std::vector<BenchmarkResult> results;

    // Hash-table operations. Pre-generated IDs and fixtures stay outside timers.
    for (std::size_t count : config.hashSizes) {
        const std::vector<std::string> ids = makeIds("HT", count);
        auto insertBatch = [&ids](std::size_t initialCapacity) {
            HashTable table(static_cast<int>(initialCapacity));
            const auto start = Clock::now();
            for (std::size_t i = 0; i < ids.size(); ++i) table.insert(ids[i], static_cast<int>(i));
            const double elapsed = elapsedMicroseconds(start);
            if (table.size() != ids.size()) throw std::runtime_error("hash insertion validation failed");
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (table.search(ids[i]) != static_cast<int>(i))
                    throw std::runtime_error("hash insertion value validation failed");
            return elapsed;
        };
        results.push_back(measure(config, "HashTable", "insertion", "uniform", count, 0, 0, count,
            [&] { (void)insertBatch(10); }, [&] { return insertBatch(10); }));

        auto searchBatch = [&ids](bool successful) {
            HashTable table(static_cast<int>(ids.size() * 2 + 1));
            for (std::size_t i = 0; i < ids.size(); ++i) table.insert(ids[i], static_cast<int>(i));
            const auto start = Clock::now();
            std::size_t sum = 0;
            const std::size_t repetitions = std::max<std::size_t>(1000, ids.size());
            for (std::size_t i = 0; i < repetitions; ++i) {
                const int found = table.search(successful ? ids[i % ids.size()] : "MISSING_KEY");
                sum += static_cast<std::size_t>(found + 1);
            }
            const double elapsed = elapsedMicroseconds(start);
            gConsumedSize = sum;
            const std::size_t remainder = repetitions % ids.size();
            const std::size_t expectedSum = successful
                ? (repetitions / ids.size()) * (ids.size() * (ids.size() + 1) / 2) +
                  remainder * (remainder + 1) / 2
                : 0;
            if (sum != expectedSum) throw std::runtime_error("hash search validation failed");
            return elapsed;
        };
        const std::size_t searchOps = std::max<std::size_t>(1000, count);
        results.push_back(measure(config, "HashTable", "successful search", "uniform", count, 0, 0,
            searchOps, [&] { (void)searchBatch(true); }, [&] { return searchBatch(true); }));
        results.push_back(measure(config, "HashTable", "unsuccessful search", "uniform", count, 0, 0,
            searchOps, [&] { (void)searchBatch(false); }, [&] { return searchBatch(false); }));

        auto resizeBatch = [&ids]() {
            HashTable table(1);
            const auto start = Clock::now();
            for (std::size_t i = 0; i < ids.size(); ++i) table.insert(ids[i], static_cast<int>(i));
            const double elapsed = elapsedMicroseconds(start);
            if (table.size() != ids.size()) throw std::runtime_error("hash resize size validation failed");
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (table.search(ids[i]) != static_cast<int>(i))
                    throw std::runtime_error("hash resize preservation validation failed");
            return elapsed;
        };
        results.push_back(measure(config, "HashTable", "automatic resizing", "uniform", count, 0, 0,
            count, [&] { (void)resizeBatch(); }, [&] { return resizeBatch(); }));
    }

    constexpr std::size_t collisionCapacity = 4096;
    for (std::size_t count : config.collisionSizes) {
        const std::vector<std::string> collisionIds = makeCollisionIds(count, collisionCapacity);
        const std::string missingId = collisionIds.back();
        std::vector<std::string> keys(collisionIds.begin(), collisionIds.end() - 1);
        for (const std::string& position : {keys.front(), keys.back(), missingId}) {
            const bool isMissing = position == missingId;
            const int expected = isMissing ? -1 : (position == keys.front() ? 0 : static_cast<int>(count - 1));
            const std::size_t batches = 100;
            auto findBatch = [&keys, &position, expected, batches]() {
                HashTable table(static_cast<int>(collisionCapacity));
                for (std::size_t i = 0; i < keys.size(); ++i) table.insert(keys[i], static_cast<int>(i));
                const auto start = Clock::now();
                int result = -1;
                for (std::size_t i = 0; i < batches; ++i) result = table.search(position);
                const double elapsed = elapsedMicroseconds(start);
                gConsumedSize = static_cast<std::size_t>(result + 1);
                if (result != expected) throw std::runtime_error("collision lookup validation failed");
                return elapsed;
            };
            const std::string which = isMissing ? "missing" :
                (position == keys.front() ? "first" : "last");
            results.push_back(measure(config, "HashTable", "collision search " + which,
                "single_bucket_chain", count, 0, 0, batches,
                [&] { (void)findBatch(); }, [&] { return findBatch(); }));
        }
    }

    // Warehouse Manager operations use fresh manager instances for every sample.
    for (std::size_t count : config.warehouseSizes) {
        const std::vector<std::string> ids = makeIds("WH", count);
        const std::vector<Warehouse> warehouses = makeWarehouses(ids);
        auto bulkInsert = [&warehouses]() {
            WarehouseManager manager;
            const auto start = Clock::now();
            for (const Warehouse& warehouse : warehouses) manager.addWarehouse(warehouse);
            const double elapsed = elapsedMicroseconds(start);
            if (manager.size() != warehouses.size()) throw std::runtime_error("warehouse count mismatch");
            for (const Warehouse& warehouse : warehouses) {
                const Warehouse* found = manager.searchWarehouse(warehouse.id);
                if (found == nullptr || found->capacity != warehouse.capacity)
                    throw std::runtime_error("warehouse insertion validation failed");
            }
            return elapsed;
        };
        results.push_back(measure(config, "WarehouseManager", "bulk insertion", "uniform", count, 0, 0,
            count, [&] { (void)bulkInsert(); }, [&] { return bulkInsert(); }));

        auto warehouseSearch = [&warehouses, &ids](bool success) {
            WarehouseManager manager;
            populateWarehouseManager(manager, warehouses);
            const std::size_t operations = std::max<std::size_t>(1000, ids.size());
            const auto start = Clock::now();
            std::size_t sum = 0;
            for (std::size_t i = 0; i < operations; ++i) {
                const Warehouse* found = manager.searchWarehouse(success ? ids[i % ids.size()] : "WH_MISSING");
                sum += found == nullptr ? 0 : static_cast<std::size_t>(found->capacity);
            }
            const double elapsed = elapsedMicroseconds(start);
            gConsumedSize = sum;
            std::size_t validCount = 0;
            for (std::size_t i = 0; i < operations; ++i)
                if ((manager.searchWarehouse(success ? ids[i % ids.size()] : "WH_MISSING") != nullptr) == success)
                    ++validCount;
            if (validCount != operations) throw std::runtime_error("warehouse search validation failed");
            return elapsed;
        };
        const std::size_t searchOps = std::max<std::size_t>(1000, count);
        results.push_back(measure(config, "WarehouseManager", "successful search", "uniform", count, 0, 0,
            searchOps, [&] { (void)warehouseSearch(true); }, [&] { return warehouseSearch(true); }));
        results.push_back(measure(config, "WarehouseManager", "unsuccessful search", "uniform", count, 0, 0,
            searchOps, [&] { (void)warehouseSearch(false); }, [&] { return warehouseSearch(false); }));

        auto updateBatch = [&warehouses, &ids]() {
            WarehouseManager manager;
            populateWarehouseManager(manager, warehouses);
            const auto start = Clock::now();
            for (const std::string& id : ids) manager.updateWarehouse(id, "Updated Depot", "Updated", 2000);
            const double elapsed = elapsedMicroseconds(start);
            for (const std::string& id : ids) {
                const Warehouse* updated = manager.searchWarehouse(id);
                if (updated == nullptr || updated->name != "Updated Depot" || updated->capacity != 2000)
                    throw std::runtime_error("warehouse update failed");
            }
            return elapsed;
        };
        results.push_back(measure(config, "WarehouseManager", "batch update", "uniform", count, 0, 0,
            count, [&] { (void)updateBatch(); }, [&] { return updateBatch(); }));

        for (bool beginning : {true, false}) {
            auto deleteOne = [&warehouses, &ids, beginning]() {
                WarehouseManager manager;
                populateWarehouseManager(manager, warehouses);
                const std::string& id = beginning ? ids.front() : ids.back();
                const auto start = Clock::now();
                const bool removed = manager.deleteWarehouse(id);
                const double elapsed = elapsedMicroseconds(start);
                if (!removed || manager.size() + 1 != warehouses.size() || manager.searchWarehouse(id) != nullptr)
                    throw std::runtime_error("warehouse deletion validation failed");
                for (const std::string& otherId : ids) {
                    if (otherId == id) continue;
                    if (manager.searchWarehouse(otherId) == nullptr)
                        throw std::runtime_error("warehouse deletion damaged another ID mapping");
                }
                return elapsed;
            };
            results.push_back(measure(config, "WarehouseManager", "delete " + std::string(beginning ? "first" : "last"),
                "uniform", count, 0, 0, 1, [&] { (void)deleteOne(); }, [&] { return deleteOne(); }));
        }
    }

    // Transportation graphs: sparse, denser and disconnected route topologies.
    std::vector<NetworkSpec> networks;
    for (std::size_t count : config.sparseGraphSizes) {
        networks.push_back(makeNetwork("sparse", count, config.seed));
        networks.push_back(makeNetwork("disconnected", count, config.seed));
    }
    for (std::size_t count : config.denseGraphSizes)
        networks.push_back(makeNetwork("dense", count, config.seed));

    for (const NetworkSpec& spec : networks) {
        const std::size_t edgeCount = spec.edges.size();
        const std::vector<std::string> constructionIds = makeIds("WH", spec.vertices);
        const std::vector<Warehouse> constructionWarehouses = makeWarehouses(constructionIds);
        auto construct = [&spec, &constructionIds, &constructionWarehouses]() {
            const auto start = Clock::now();
            double elapsed = 0.0;
            std::size_t actualRoutes = 0;
            withNetworkData(spec, constructionIds, constructionWarehouses,
                [&](const WarehouseManager&, const TransportationGraph& graph,
                    const std::vector<std::string>&) {
                elapsed = elapsedMicroseconds(start);
                actualRoutes = graph.routeCount();
            });
            if (actualRoutes != spec.edges.size()) throw std::runtime_error("graph construction validation failed");
            gConsumedSize = actualRoutes;
            return elapsed;
        };
        results.push_back(measure(config, "TransportationGraph", "graph construction", spec.workload,
            spec.vertices, spec.vertices, edgeCount, spec.vertices + edgeCount,
            [&] { (void)construct(); }, [&] { return construct(); }));

        auto routeInsert = [&spec]() {
            WarehouseManager manager;
            const std::vector<std::string> ids = makeIds("WH", spec.vertices);
            populateWarehouseManager(manager, makeWarehouses(ids));
            TransportationGraph graph(manager);
            for (const std::string& id : ids) graph.registerWarehouseVertex(id);
            const auto start = Clock::now();
            for (const EdgeSpec& edge : spec.edges) {
                const double distance = edge.second == edge.first + 1
                    ? 1.0 : static_cast<double>(spec.vertices + 1);
                graph.addRoute(ids[edge.first], ids[edge.second], distance);
            }
            const double elapsed = elapsedMicroseconds(start);
            if (graph.routeCount() != spec.edges.size()) throw std::runtime_error("route count mismatch");
            return elapsed;
        };
        results.push_back(measure(config, "TransportationGraph", "route insertion", spec.workload,
            spec.vertices, spec.vertices, edgeCount, edgeCount,
            [&] { (void)routeInsert(); }, [&] { return routeInsert(); }));

        const std::string source = numberedId("WH", 0);
        const std::string target = numberedId("WH", spec.vertices == 0 ? 0 : spec.vertices - 1);
        auto dijkstra = [&spec, &source, &target]() {
            double elapsed = 0;
            withNetwork(spec, [&](const WarehouseManager&, const TransportationGraph& graph,
                                  const std::vector<std::string>& ids) {
                const auto start = Clock::now();
                const ShortestPathResult result = GraphAlgorithms::dijkstra(graph, source, target);
                elapsed = elapsedMicroseconds(start);
                if (spec.workload == "disconnected" && spec.vertices > 1) {
                    if (result.status != ShortestPathStatus::Unreachable)
                        throw std::runtime_error("Dijkstra disconnected validation failed");
                } else if (spec.vertices > 0) {
                    if (result.status != ShortestPathStatus::Found || result.warehouseIds.empty() ||
                        !std::isfinite(result.totalDistanceKm))
                        throw std::runtime_error("Dijkstra validation failed");
                    gConsumedDistance = result.totalDistanceKm;
                    if (result.totalDistanceKm != static_cast<double>(spec.vertices - 1))
                        throw std::runtime_error("Dijkstra distance differs from known backbone distance");
                    if (result.warehouseIds.front() != ids.front() || result.warehouseIds.back() != ids.back())
                        throw std::runtime_error("Dijkstra path endpoints incorrect");
                }
            });
            return elapsed;
        };
        results.push_back(measure(config, "TransportationGraph", "Dijkstra", spec.workload,
            spec.vertices, spec.vertices, edgeCount, 1,
            [&] { (void)dijkstra(); }, [&] { return dijkstra(); }));

        auto traversal = [&spec](bool breadth) {
            double elapsed = 0;
            withNetwork(spec, [&](const WarehouseManager&, const TransportationGraph& graph,
                                  const std::vector<std::string>&) {
                const auto start = Clock::now();
                const TraversalResult result = breadth
                    ? GraphAlgorithms::breadthFirstSearch(graph, numberedId("WH", 0))
                    : GraphAlgorithms::depthFirstSearch(graph, numberedId("WH", 0));
                elapsed = elapsedMicroseconds(start);
                const std::size_t expected = spec.vertices == 0 ? 0 : spec.reachableFromZero;
                if (!result.validStart || result.warehouseIds.size() != expected)
                    throw std::runtime_error("graph traversal validation failed");
                std::set<std::string> visited(result.warehouseIds.begin(), result.warehouseIds.end());
                if (visited.size() != expected || visited.find(numberedId("WH", 0)) == visited.end())
                    throw std::runtime_error("graph traversal contains duplicate or missing start IDs");
                for (std::size_t i = 0; i < expected; ++i)
                    if (visited.find(numberedId("WH", i)) == visited.end())
                        throw std::runtime_error("graph traversal omitted a reachable warehouse");
                gConsumedSize = result.warehouseIds.size();
            });
            return elapsed;
        };
        for (bool breadth : {true, false})
            results.push_back(measure(config, "TransportationGraph", breadth ? "BFS" : "DFS",
                spec.workload, spec.vertices, spec.vertices, edgeCount, 1,
                [&] { (void)traversal(breadth); }, [&] { return traversal(breadth); }));
    }

    // Fulfillment workloads: independent nodes, chains, branches, and cycles.
    for (std::size_t count : config.fulfillmentSizes) {
        const std::vector<std::string> ids = makeIds("OP", count);
        for (const std::string& workload : {"independent", "chain", "branching", "cyclic"}) {
            const std::vector<EdgeSpec> edges = fulfillmentEdges(workload, count);
            auto operationInsert = [&ids]() {
                FulfillmentGraph graph;
                const auto start = Clock::now();
                for (const std::string& id : ids) graph.addOperation(id);
                const double elapsed = elapsedMicroseconds(start);
                if (graph.getOperationIds().size() != ids.size()) throw std::runtime_error("operation count mismatch");
                return elapsed;
            };
            results.push_back(measure(config, "FulfillmentGraph", "operation insertion", workload,
                count, count, edges.size(), count,
                [&] { (void)operationInsert(); }, [&] { return operationInsert(); }));

            auto dependencyInsert = [&ids, &edges]() {
                FulfillmentGraph graph;
                for (const std::string& id : ids) graph.addOperation(id);
                const auto start = Clock::now();
                for (const EdgeSpec& edge : edges)
                    graph.addDependency(ids[edge.first], ids[edge.second]);
                const double elapsed = elapsedMicroseconds(start);
                if (graph.getDependencies().size() != edges.size())
                    throw std::runtime_error("dependency count mismatch");
                return elapsed;
            };
            results.push_back(measure(config, "FulfillmentGraph", "dependency insertion", workload,
                count, count, edges.size(), edges.size(),
                [&] { (void)dependencyInsert(); }, [&] { return dependencyInsert(); }));

            auto cycleDetection = [&ids, &edges, &workload]() {
                FulfillmentGraph graph;
                if (!setupFulfillment(graph, ids, edges)) throw std::runtime_error("fulfillment setup failed");
                const auto start = Clock::now();
                const bool hasCycle = graph.hasCycle();
                const double elapsed = elapsedMicroseconds(start);
                if (hasCycle != (workload == "cyclic")) throw std::runtime_error("cycle result incorrect");
                return elapsed;
            };
            results.push_back(measure(config, "FulfillmentGraph", "cycle detection", workload,
                count, count, edges.size(), 1,
                [&] { (void)cycleDetection(); }, [&] { return cycleDetection(); }));

            auto topological = [&ids, &edges, &workload]() {
                FulfillmentGraph graph;
                if (!setupFulfillment(graph, ids, edges)) throw std::runtime_error("fulfillment setup failed");
                const auto start = Clock::now();
                const TopologicalSortResult sorted = graph.topologicalSort();
                const double elapsed = elapsedMicroseconds(start);
                if (workload == "cyclic") {
                    if (sorted.status != TopologicalSortStatus::CycleDetected || !sorted.operationIds.empty())
                        throw std::runtime_error("cyclic topological result incorrect");
                } else if (sorted.status != TopologicalSortStatus::Success || sorted.operationIds.size() != ids.size()) {
                    throw std::runtime_error("topological order validation failed");
                } else {
                    std::map<std::string, std::size_t> positionById;
                    for (std::size_t i = 0; i < sorted.operationIds.size(); ++i)
                        if (!positionById.emplace(sorted.operationIds[i], i).second)
                            throw std::runtime_error("topological order repeats an operation");
                    if (positionById.size() != ids.size())
                        throw std::runtime_error("topological order omitted an operation");
                    for (const EdgeSpec& edge : edges)
                        if (positionById[ids[edge.first]] >= positionById[ids[edge.second]])
                            throw std::runtime_error("topological order violates a dependency");
                }
                gConsumedSize = sorted.operationIds.size();
                return elapsed;
            };
            results.push_back(measure(config, "FulfillmentGraph", "topological sort", workload,
                count, count, edges.size(), 1,
                [&] { (void)topological(); }, [&] { return topological(); }));
        }
    }
    return results;
}

std::vector<ConcurrentBenchmarkResult> BenchmarkRunner::runConcurrent(
    const ConcurrentBenchmarkConfig& concurrentConfig) const {
    if (concurrentConfig.workerCounts.empty() || concurrentConfig.datasetSize < 2 ||
        concurrentConfig.operationsPerRepetition == 0 || concurrentConfig.repetitions < 2 ||
        concurrentConfig.warmupOperations == 0 ||
        concurrentConfig.operationsPerRepetition > concurrentConfig.datasetSize * 3) {
        throw std::invalid_argument("invalid concurrent benchmark configuration");
    }
    for (std::size_t workers : concurrentConfig.workerCounts) {
        if (workers == 0) {
            throw std::invalid_argument("worker counts must be positive");
        }
    }

    const std::vector<std::string> workloads{"read-heavy", "write-heavy", "mixed"};
    std::vector<ConcurrentBenchmarkResult> results;
    results.reserve(workloads.size() * concurrentConfig.workerCounts.size());

    for (const std::string& workload : workloads) {
        for (std::size_t workers : concurrentConfig.workerCounts) {
            // Warm-up uses a separate service/pool and is excluded from metrics.
            const std::size_t warmupCount = std::min(
                concurrentConfig.warmupOperations,
                concurrentConfig.operationsPerRepetition);
            (void)runConcurrentBatch(workload, workers, concurrentConfig.datasetSize,
                                     warmupCount);

            ConcurrentBenchmarkResult result;
            result.workloadType = workload;
            result.workerCount = workers;
            result.datasetSize = concurrentConfig.datasetSize;
            result.repetitions = concurrentConfig.repetitions;
            std::vector<double> latencies;
            latencies.reserve(concurrentConfig.operationsPerRepetition *
                              concurrentConfig.repetitions);
            for (std::size_t repetition = 0;
                 repetition < concurrentConfig.repetitions; ++repetition) {
                const ConcurrentBatchSample sample = runConcurrentBatch(
                    workload, workers, concurrentConfig.datasetSize,
                    concurrentConfig.operationsPerRepetition);
                result.totalOperations += sample.completedOperations;
                result.failedOperations += sample.failedOperations;
                result.totalElapsedMilliseconds += sample.elapsedMilliseconds;
                latencies.insert(latencies.end(), sample.latencies.begin(),
                                 sample.latencies.end());
            }

            if (!latencies.empty()) {
                const double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
                result.averageLatencyMicroseconds = sum / latencies.size();
                result.p50LatencyMicroseconds = percentile(latencies, 0.50);
                result.p95LatencyMicroseconds = percentile(latencies, 0.95);
                result.p99LatencyMicroseconds = percentile(latencies, 0.99);
            }
            const double elapsedSeconds = result.totalElapsedMilliseconds / 1000.0;
            result.throughputOperationsPerSecond = elapsedSeconds > 0.0
                ? static_cast<double>(result.totalOperations) / elapsedSeconds
                : 0.0;
            results.push_back(std::move(result));
        }
    }
    return results;
}

bool BenchmarkRunner::writeCsv(const std::string& path,
                               const std::vector<BenchmarkResult>& results,
                               std::string& errorMessage) {
    errorMessage.clear();
    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        errorMessage = "unable to open output file";
        return false;
    }
    output << "component,operation,workload,input_size,vertices,edges,repetitions,operations_per_sample,avg_us,median_us,min_us,max_us,time_unit,seed,build_mode\n";
    for (const BenchmarkResult& result : results) {
        if (!std::isfinite(result.averageMicroseconds) || !std::isfinite(result.medianMicroseconds) ||
            !std::isfinite(result.minimumMicroseconds) || !std::isfinite(result.maximumMicroseconds) ||
            result.averageMicroseconds < 0.0 || result.medianMicroseconds < 0.0 ||
            result.minimumMicroseconds < 0.0 || result.maximumMicroseconds < 0.0) {
            errorMessage = "result contains an invalid timing value";
            return false;
        }
        writeRow(output, result);
        if (!output) {
            errorMessage = "write failed while recording results";
            return false;
        }
    }
    output.flush();
    if (!output) {
        errorMessage = "flush failed while recording results";
        return false;
    }
    return true;
}

bool BenchmarkRunner::writeConcurrentCsv(
    const std::string& path,
    const std::vector<ConcurrentBenchmarkResult>& results,
    std::string& errorMessage) {
    errorMessage.clear();
    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        errorMessage = "unable to open output file";
        return false;
    }
    output << "workload_type,worker_count,dataset_size,total_operations,repetitions,"
              "average_latency_us,p50_latency_us,p95_latency_us,p99_latency_us,"
              "throughput_operations_per_second,total_elapsed_ms,failed_operations\n";
    for (const ConcurrentBenchmarkResult& result : results) {
        const double metrics[] = {
            result.averageLatencyMicroseconds,
            result.p50LatencyMicroseconds,
            result.p95LatencyMicroseconds,
            result.p99LatencyMicroseconds,
            result.throughputOperationsPerSecond,
            result.totalElapsedMilliseconds};
        for (double value : metrics) {
            if (!std::isfinite(value) || value < 0.0) {
                errorMessage = "result contains an invalid concurrent metric";
                return false;
            }
        }
        output << escapeCsv(result.workloadType) << ',' << result.workerCount << ','
               << result.datasetSize << ',' << result.totalOperations << ','
               << result.repetitions << ',' << std::setprecision(12)
               << result.averageLatencyMicroseconds << ','
               << result.p50LatencyMicroseconds << ',' << result.p95LatencyMicroseconds << ','
               << result.p99LatencyMicroseconds << ','
               << result.throughputOperationsPerSecond << ','
               << result.totalElapsedMilliseconds << ',' << result.failedOperations << '\n';
        if (!output) {
            errorMessage = "write failed while recording concurrent results";
            return false;
        }
    }
    output.flush();
    if (!output) {
        errorMessage = "flush failed while recording concurrent results";
        return false;
    }
    return true;
}
