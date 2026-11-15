#include "thread_pool.h"
#include "smartlogix_service.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
int checks = 0;
int passedChecks = 0;

class TestGate {
public:
    explicit TestGate(std::size_t participantCount)
        : participantCount_(participantCount) {}

    void workerWaits() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return released_; });
    }

    bool waitForAll(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout,
                                 [this]() { return arrived_ == participantCount_; });
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

private:
    const std::size_t participantCount_;
    std::size_t arrived_ = 0;
    bool released_ = false;
    std::mutex mutex_;
    std::condition_variable changed_;
};

void check(bool condition, const std::string& description) {
    ++checks;
    if (condition) {
        ++passedChecks;
        std::cout << "[PASS] " << description << '\n';
    } else {
        ++failures;
        std::cout << "[FAIL] " << description << '\n';
    }
}

void testConfiguration() {
    bool zeroWorkersRejected = false;
    bool zeroCapacityRejected = false;
    try {
        ThreadPool invalid(0, 1);
    } catch (const std::invalid_argument&) {
        zeroWorkersRejected = true;
    }
    try {
        ThreadPool invalid(1, 0);
    } catch (const std::invalid_argument&) {
        zeroCapacityRejected = true;
    }

    ThreadPool pool(2, 3);
    check(zeroWorkersRejected && zeroCapacityRejected,
          "zero worker count and queue capacity are rejected");
    check(pool.workerCount() == 2 && pool.queueCapacity() == 3,
          "worker count and queue capacity are configurable");
}

void testConcurrentTaskResults() {
    ThreadPool pool(4, 16);
    std::vector<std::future<int>> results;
    for (int value = 0; value < 12; ++value) {
        results.push_back(pool.submit([value]() { return value * value; }));
    }

    bool correct = true;
    for (int value = 0; value < static_cast<int>(results.size()); ++value) {
        correct = correct && results[static_cast<std::size_t>(value)].get() == value * value;
    }
    check(correct, "submitted tasks execute and return future results");
    pool.shutdown();
}

void testMultipleWorkersRunAtOnce() {
    constexpr std::size_t workerCount = 4;
    ThreadPool pool(workerCount, workerCount);
    TestGate startGate(workerCount);
    TestGate activeGate(workerCount);
    std::atomic<std::size_t> active{0};
    std::atomic<std::size_t> maxActive{0};
    std::vector<std::future<void>> tasks;

    for (std::size_t i = 0; i < workerCount; ++i) {
        tasks.push_back(pool.submit([&]() {
            startGate.workerWaits();
            const std::size_t nowActive = ++active;
            std::size_t observed = maxActive.load();
            while (observed < nowActive &&
                   !maxActive.compare_exchange_weak(observed, nowActive)) {
            }
            activeGate.workerWaits();
            --active;
        }));
    }

    const bool allWorkersStarted = startGate.waitForAll();
    startGate.release();
    const bool allWorkersOverlapped = activeGate.waitForAll();
    const bool simultaneous = allWorkersOverlapped && maxActive == workerCount;
    activeGate.release();
    for (std::future<void>& task : tasks) task.get();
    check(allWorkersStarted && simultaneous,
          "all configured workers execute tasks simultaneously");
    pool.shutdown();
}

void testQueueSaturation() {
    ThreadPool pool(1, 1);
    std::promise<void> taskStarted;
    std::promise<void> releaseTask;
    std::shared_future<void> releaseSignal = releaseTask.get_future().share();

    std::future<void> running = pool.submit([&taskStarted, releaseSignal]() {
        taskStarted.set_value();
        releaseSignal.wait();
    });
    taskStarted.get_future().wait();

    std::future<int> queued = pool.submit([]() { return 42; });
    bool rejected = false;
    try {
        (void)pool.submit([]() { return 7; });
    } catch (const QueueFull&) {
        rejected = true;
    }
    check(rejected && pool.queuedTaskCount() == 1,
          "bounded queue rejects new work when full");

    releaseTask.set_value();
    running.get();
    check(queued.get() == 42, "queued work runs after capacity becomes available");
    pool.shutdown();
}

void testExceptionPropagation() {
    ThreadPool pool(1, 2);
    std::future<void> result = pool.submit([]() {
        throw std::runtime_error("worker failure");
    });

    bool propagated = false;
    try {
        result.get();
    } catch (const std::runtime_error& error) {
        propagated = std::string(error.what()) == "worker failure";
    }
    check(propagated, "worker exception is rethrown by future.get()");

    std::future<int> followup = pool.submit([]() { return 9; });
    check(followup.get() == 9, "worker remains available after task exception");
    pool.shutdown();
}

void testShutdownDrainsAcceptedWork() {
    ThreadPool pool(1, 8);
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    std::promise<void> taskStarted;
    std::promise<void> releaseTask;
    const std::shared_future<void> releaseSignal = releaseTask.get_future().share();
    futures.push_back(pool.submit([&]() {
        taskStarted.set_value();
        releaseSignal.wait();
        ++completed;
    }));
    taskStarted.get_future().wait();
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool.submit([&completed]() { ++completed; }));
    }
    const bool tasksWereQueued = pool.queuedTaskCount() == 5;

    std::promise<void> shutdownStarted;
    std::thread shutdownThread([&]() {
        shutdownStarted.set_value();
        pool.shutdown();
    });
    shutdownStarted.get_future().wait();

    bool stoppedAccepting = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !stoppedAccepting) {
        if (!pool.acceptingTasks()) {
            stoppedAccepting = true;
            break;
        }
        std::this_thread::yield();
    }
    releaseTask.set_value();
    shutdownThread.join();

    bool allFuturesReady = true;
    for (std::future<void>& future : futures) {
        try {
            future.get();
        } catch (...) {
            allFuturesReady = false;
        }
    }
    check(tasksWereQueued && stoppedAccepting && allFuturesReady && completed == 6,
          "shutdown while work is queued stops acceptance, drains, and joins workers");

    bool submitAfterShutdownRejected = false;
    try {
        (void)pool.submit([]() {});
    } catch (const std::runtime_error&) {
        submitAfterShutdownRejected = true;
    }
    check(submitAfterShutdownRejected, "pool rejects submissions after shutdown");
    pool.shutdown();
}

void testRepeatedThreadPoolLifecycle() {
    bool allResultsCorrect = true;
    for (int iteration = 0; iteration < 40; ++iteration) {
        const std::size_t workers = static_cast<std::size_t>(iteration % 4 + 1);
        {
            ThreadPool pool(workers, 16);
            std::vector<std::future<int>> results;
            for (int task = 0; task < 8; ++task) {
                results.push_back(pool.submit([iteration, task]() {
                    return iteration * 10 + task;
                }));
            }
            for (int task = 0; task < 8; ++task) {
                allResultsCorrect = allResultsCorrect &&
                                    results[static_cast<std::size_t>(task)].get() ==
                                        iteration * 10 + task;
            }
        }
    }
    check(allResultsCorrect,
          "repeated ThreadPool creation and destructor shutdown complete cleanly");
}

void testServiceWarehouseAndGraphOperations() {
    SmartLogixService service;
    const Warehouse alpha{"WH-A", "Alpha", "North", 80};
    const Warehouse beta{"WH-B", "Beta", "South", 60};

    const bool added = service.addWarehouse(alpha) && service.addWarehouse(beta);
    const bool duplicateRejected = !service.addWarehouse(alpha);
    std::optional<Warehouse> found = service.searchWarehouse("WH-A");
    const bool lookupCopiedValue = found.has_value() && found->name == "Alpha";
    if (found) {
        found->name = "changed outside the service";
    }
    const auto unchanged = service.searchWarehouse("WH-A");
    const bool copyDoesNotChangeStoredValue = unchanged && unchanged->name == "Alpha";
    const bool updated = service.updateWarehouse("WH-A", "Alpha Hub", "North", 90);
    const auto afterUpdate = service.searchWarehouse("WH-A");
    const bool updateCorrect = updated && afterUpdate && afterUpdate->name == "Alpha Hub" &&
                               afterUpdate->capacity == 90;

    const bool routeAdded = service.addRoute("WH-A", "WH-B", 12.5);
    const bool duplicateRouteRejected = !service.addRoute("WH-A", "WH-B", 20.0);
    const auto outgoing = service.getOutgoingRoutes("WH-A");
    const ShortestPathResult path = service.shortestPath("WH-A", "WH-B");
    const TraversalResult bfs = service.breadthFirstSearch("WH-A");
    const TraversalResult dfs = service.depthFirstSearch("WH-A");
    const bool graphResultsCorrect = outgoing && outgoing->size() == 1 &&
                                     outgoing->front().distanceKm == 12.5 &&
                                     path.status == ShortestPathStatus::Found &&
                                     path.warehouseIds.size() == 2 &&
                                     bfs.validStart && bfs.warehouseIds.size() == 2 &&
                                     dfs.validStart && dfs.warehouseIds.size() == 2;

    const bool removed = service.deleteWarehouse("WH-A");
    const bool graphRemainsConsistent = !service.searchWarehouse("WH-A") &&
                                        service.vertexCount() == 1 &&
                                        service.routeCount() == 0;

    check(added && duplicateRejected, "service adds warehouses and rejects duplicate IDs");
    check(lookupCopiedValue && copyDoesNotChangeStoredValue,
          "warehouse lookup returns an independent value copy");
    check(updateCorrect, "warehouse update preserves existing field semantics");
    check(routeAdded && duplicateRouteRejected && graphResultsCorrect,
          "service preserves route and graph algorithm behavior");
    check(removed && graphRemainsConsistent,
          "warehouse deletion removes its transportation vertex and routes");
}

void testServiceFulfillmentOperations() {
    SmartLogixService service;
    const bool added = service.addOperation("PICK") && service.addOperation("PACK") &&
                       service.addOperation("SHIP");
    const bool duplicateRejected = !service.addOperation("PACK");
    const bool dependencyAdded = service.addDependency("PICK", "PACK") &&
                                 service.addDependency("PACK", "SHIP");
    const TopologicalSortResult result = service.topologicalSort();
    const auto operations = service.getOperationIds();
    const auto dependencies = service.getDependencies();

    check(added && duplicateRejected,
          "service adds fulfillment operations and rejects duplicate IDs");
    check(dependencyAdded && !service.hasFulfillmentCycle() &&
              result.status == TopologicalSortStatus::Success &&
              result.operationIds.size() == 3 && result.operationIds.front() == "PICK" &&
              operations.size() == 3 && dependencies.size() == 2,
          "service preserves fulfillment dependency and topological behavior");
}

void testServiceCanBeSubmittedToThreadPool() {
    SmartLogixService service;
    service.addWarehouse({"WH-POOL", "Pool warehouse", "Central", 10});
    ThreadPool pool(1, 2);
    std::future<std::optional<Warehouse>> lookup = pool.submit(
        &SmartLogixService::searchWarehouse, &service, std::string("WH-POOL"));
    const auto result = lookup.get();
    check(result && result->id == "WH-POOL",
          "service value-returning operation works with ThreadPool futures");
    pool.shutdown();
}

void testConcurrentNetworkOperationsStayConsistent() {
    SmartLogixService service;
    constexpr int warehouseCount = 80;
    bool initialized = true;
    for (int i = 0; i < warehouseCount; ++i) {
        const std::string id = "WH-" + std::to_string(i);
        initialized = initialized &&
                      service.addWarehouse({id, "Warehouse " + std::to_string(i),
                                            "Test", 100});
    }

    ThreadPool pool(8, 256);
    std::vector<std::future<bool>> routeWriters;
    for (int i = 0; i + 1 < warehouseCount; ++i) {
        const std::string from = "WH-" + std::to_string(i);
        const std::string to = "WH-" + std::to_string(i + 1);
        routeWriters.push_back(pool.submit([&service, from, to]() {
            return service.addRoute(from, to, 1.0);
        }));
    }

    std::vector<std::future<bool>> readers;
    for (int i = 0; i < 40; ++i) {
        readers.push_back(pool.submit([&service]() {
            const auto warehouses = service.getAllWarehouses();
            const auto routes = service.getAllRoutes();
            const TraversalResult traversal = service.breadthFirstSearch("WH-0");
            return warehouses.size() == warehouseCount &&
                   routes.size() <= static_cast<std::size_t>(warehouseCount - 1) &&
                   traversal.validStart && !traversal.warehouseIds.empty();
        }));
    }

    bool routesAdded = initialized;
    for (std::future<bool>& result : routeWriters) {
        routesAdded = result.get() && routesAdded;
    }
    bool readsValid = true;
    for (std::future<bool>& result : readers) {
        readsValid = result.get() && readsValid;
    }

    constexpr std::size_t graphWorkers = 8;
    TestGate graphGate(graphWorkers);
    std::vector<std::future<bool>> graphWorkersResults;
    for (std::size_t worker = 0; worker < graphWorkers; ++worker) {
        graphWorkersResults.push_back(pool.submit([&service, &graphGate, worker]() {
            graphGate.workerWaits();
            bool valid = true;
            const std::size_t firstEdge = worker * 10;
            const std::size_t endEdge = std::min<std::size_t>(firstEdge + 10, 79);
            for (std::size_t i = firstEdge; i < endEdge; ++i) {
                const std::string from = "WH-" + std::to_string(i);
                const std::string to = "WH-" + std::to_string(i + 1);
                valid = service.removeRoute(from, to) && valid;
                const ShortestPathResult shortest = service.shortestPath("WH-0", "WH-79");
                const TraversalResult bfs = service.breadthFirstSearch("WH-0");
                const TraversalResult dfs = service.depthFirstSearch("WH-0");
                const bool validShortest = shortest.status == ShortestPathStatus::Found ||
                                           shortest.status == ShortestPathStatus::Unreachable;
                valid = valid && validShortest && bfs.validStart && dfs.validStart;
            }
            return valid;
        }));
    }
    const bool graphWorkersReady = graphGate.waitForAll();
    graphGate.release();
    bool graphOperationsValid = graphWorkersReady;
    for (std::future<bool>& result : graphWorkersResults) {
        graphOperationsValid = result.get() && graphOperationsValid;
    }

    std::vector<std::future<bool>> deleters;
    for (int i = 0; i < 20; ++i) {
        const std::string id = "WH-" + std::to_string(i);
        deleters.push_back(pool.submit([&service, id]() {
            return service.deleteWarehouse(id);
        }));
    }
    bool deletionsSucceeded = true;
    for (std::future<bool>& result : deleters) {
        deletionsSucceeded = result.get() && deletionsSucceeded;
    }

    const std::vector<Warehouse> remaining = service.getAllWarehouses();
    const std::vector<Route> remainingRoutes = service.getAllRoutes();
    std::vector<std::string> remainingIds;
    for (const Warehouse& warehouse : remaining) {
        remainingIds.push_back(warehouse.id);
    }
    bool routesHaveValidEndpoints = true;
    bool everyWarehouseHasVertex = true;
    for (const Warehouse& warehouse : remaining) {
        everyWarehouseHasVertex = everyWarehouseHasVertex &&
                                  service.getOutgoingRoutes(warehouse.id).has_value();
    }
    for (const Route& route : remainingRoutes) {
        bool fromFound = false;
        bool toFound = false;
        for (const std::string& id : remainingIds) {
            fromFound = fromFound || route.fromWarehouseId == id;
            toFound = toFound || route.toWarehouseId == id;
        }
        routesHaveValidEndpoints = routesHaveValidEndpoints && fromFound && toFound;
    }

    check(initialized && routesAdded && readsValid,
          "concurrent network writes and graph queries complete without invalid results");
    check(graphOperationsValid && remainingRoutes.empty(),
          "coordinated route removals overlap Dijkstra, BFS, and DFS safely");
    check(deletionsSucceeded && remaining.size() == 60 &&
              service.vertexCount() == remaining.size() &&
              everyWarehouseHasVertex && routesHaveValidEndpoints &&
              service.routeCount() == remainingRoutes.size(),
          "concurrent warehouse deletion preserves warehouse and graph consistency");
    pool.shutdown();
}

void testConcurrentFulfillmentOperations() {
    SmartLogixService service;
    ThreadPool pool(8, 256);
    std::vector<std::future<bool>> operationWriters;
    for (int i = 0; i < 80; ++i) {
        const std::string id = "OP-" + std::to_string(i);
        operationWriters.push_back(pool.submit([&service, id]() {
            return service.addOperation(id);
        }));
    }
    bool operationsAdded = true;
    for (std::future<bool>& result : operationWriters) {
        operationsAdded = result.get() && operationsAdded;
    }

    std::vector<std::future<bool>> dependencyWriters;
    for (int i = 0; i < 40; ++i) {
        const std::string prerequisite = "OP-" + std::to_string(i * 2);
        const std::string dependent = "OP-" + std::to_string(i * 2 + 1);
        dependencyWriters.push_back(pool.submit(
            [&service, prerequisite, dependent]() {
                return service.addDependency(prerequisite, dependent);
            }));
    }

    std::vector<std::future<bool>> readers;
    for (int i = 0; i < 24; ++i) {
        readers.push_back(pool.submit([&service]() {
            const auto ids = service.getOperationIds();
            const TopologicalSortResult sorted = service.topologicalSort();
            return ids.size() == 80 && sorted.status == TopologicalSortStatus::Success &&
                   !service.hasFulfillmentCycle();
        }));
    }

    bool dependenciesAdded = true;
    for (std::future<bool>& result : dependencyWriters) {
        dependenciesAdded = result.get() && dependenciesAdded;
    }
    bool readsValid = true;
    for (std::future<bool>& result : readers) {
        readsValid = result.get() && readsValid;
    }
    check(operationsAdded && dependenciesAdded,
          "concurrent fulfillment insertions accept all unique records");
    check(readsValid,
          "cycle and topological readers remain valid during dependency writes");
    const std::size_t dependencyCount = service.getDependencies().size();
    check(dependencyCount == 40,
          "concurrent fulfillment mutations and queries preserve valid DAG results");
    const bool cycleOperationsAdded = service.addOperation("CYCLE-A") &&
                                      service.addOperation("CYCLE-B") &&
                                      service.addDependency("CYCLE-A", "CYCLE-B") &&
                                      service.addDependency("CYCLE-B", "CYCLE-A");
    const TopologicalSortResult cycleResult = service.topologicalSort();
    check(cycleOperationsAdded && service.hasFulfillmentCycle() &&
              cycleResult.status == TopologicalSortStatus::CycleDetected &&
              cycleResult.operationIds.empty(),
          "cycle detection and topological sorting preserve cycle semantics");
    pool.shutdown();
}

void testConcurrentDuplicateInsertion() {
    SmartLogixService service;
    ThreadPool pool(8, 32);
    constexpr std::size_t attempts = 8;
    TestGate warehouseGate(attempts);
    std::atomic<int> warehouseWinners{0};
    std::vector<std::future<void>> warehouseAttempts;
    for (std::size_t i = 0; i < attempts; ++i) {
        warehouseAttempts.push_back(pool.submit([&]() {
            warehouseGate.workerWaits();
            if (service.addWarehouse({"SAME-WH", "Same", "Test", 1})) {
                ++warehouseWinners;
            }
        }));
    }
    const bool warehouseWorkersReady = warehouseGate.waitForAll();
    warehouseGate.release();
    for (std::future<void>& result : warehouseAttempts) result.get();

    TestGate operationGate(attempts);
    std::atomic<int> operationWinners{0};
    std::vector<std::future<void>> operationAttempts;
    for (std::size_t i = 0; i < attempts; ++i) {
        operationAttempts.push_back(pool.submit([&]() {
            operationGate.workerWaits();
            if (service.addOperation("SAME-OP")) {
                ++operationWinners;
            }
        }));
    }
    const bool operationWorkersReady = operationGate.waitForAll();
    operationGate.release();
    for (std::future<void>& result : operationAttempts) result.get();

    TestGate lookupGate(attempts);
    std::vector<std::future<bool>> simultaneousLookups;
    for (std::size_t i = 0; i < attempts; ++i) {
        simultaneousLookups.push_back(pool.submit([&service, &lookupGate]() {
            lookupGate.workerWaits();
            const auto warehouse = service.searchWarehouse("SAME-WH");
            const auto operations = service.getOperationIds();
            return warehouse.has_value() && operations.size() == 1;
        }));
    }
    const bool lookupWorkersReady = lookupGate.waitForAll();
    lookupGate.release();
    bool lookupsValid = true;
    for (std::future<bool>& result : simultaneousLookups) {
        lookupsValid = result.get() && lookupsValid;
    }

    check(warehouseWorkersReady && warehouseWinners == 1 &&
              service.getAllWarehouses().size() == 1 && service.vertexCount() == 1,
          "simultaneous duplicate warehouse insertions produce one consistent record");
    check(operationWorkersReady && operationWinners == 1 &&
              service.getOperationIds().size() == 1,
          "simultaneous duplicate fulfillment insertions produce one operation");
    check(lookupWorkersReady && lookupsValid,
          "multiple simultaneous service lookups return consistent copied values");
    pool.shutdown();
}

void testMixedStressAcrossWorkerCounts() {
    const std::vector<std::size_t> workerCounts{1, 2, 4, 8};
    constexpr int repetitions = 3;
    bool everyRunValid = true;
    int runsCompleted = 0;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        for (const std::size_t workerCount : workerCounts) {
            SmartLogixService service;
            const bool baseReady =
                service.addWarehouse({"BASE-0", "Base 0", "Test", 10}) &&
                service.addWarehouse({"BASE-1", "Base 1", "Test", 10}) &&
                service.addRoute("BASE-0", "BASE-1", 1.0);
            ThreadPool pool(workerCount, workerCount * 4 + 16);
            TestGate startGate(workerCount);
            std::atomic<int> duplicateWarehouseWinners{0};
            std::atomic<int> duplicateOperationWinners{0};
            std::vector<std::future<bool>> workers;

            for (std::size_t worker = 0; worker < workerCount; ++worker) {
                workers.push_back(pool.submit([&, worker, repetition]() {
                    startGate.workerWaits();
                    bool valid = service.searchWarehouse("BASE-0").has_value();
                    if (service.addWarehouse({"SHARED-WH", "Shared", "Test", 5})) {
                        ++duplicateWarehouseWinners;
                    }
                    if (service.addOperation("SHARED-OP")) {
                        ++duplicateOperationWinners;
                    }

                    for (int item = 0; item < 10; ++item) {
                        const std::string suffix = std::to_string(repetition) + "-" +
                                                   std::to_string(worker) + "-" +
                                                   std::to_string(item);
                        const std::string warehouseId = "RUN-WH-" + suffix;
                        const std::string operationId = "RUN-OP-" + suffix;
                        valid = service.addWarehouse(
                                    {warehouseId, "Run warehouse", "Test", 20}) && valid;
                        valid = service.addRoute("BASE-0", warehouseId, 2.0) && valid;
                        const auto record = service.searchWarehouse(warehouseId);
                        const ShortestPathResult shortest =
                            service.shortestPath("BASE-0", warehouseId);
                        const TraversalResult bfs = service.breadthFirstSearch("BASE-0");
                        const TraversalResult dfs = service.depthFirstSearch("BASE-0");
                        valid = valid && record.has_value() &&
                                shortest.status == ShortestPathStatus::Found &&
                                bfs.validStart && dfs.validStart;

                        valid = service.addOperation(operationId) && valid;
                        valid = service.addDependency("SHARED-OP", operationId) && valid;
                        valid = !service.hasFulfillmentCycle() &&
                                service.topologicalSort().status ==
                                    TopologicalSortStatus::Success && valid;

                        if ((item % 2) == 0) {
                            valid = service.removeRoute("BASE-0", warehouseId) && valid;
                            valid = service.deleteWarehouse(warehouseId) && valid;
                        } else {
                            valid = service.updateWarehouse(warehouseId,
                                                            "Updated warehouse",
                                                            "Test", 25) && valid;
                        }
                    }
                    return valid;
                }));
            }

            const bool workersReady = startGate.waitForAll();
            startGate.release();
            bool workerResultsValid = true;
            for (std::future<bool>& result : workers) {
                workerResultsValid = result.get() && workerResultsValid;
            }

            std::vector<Warehouse> warehouses = service.getAllWarehouses();
            std::vector<Route> routes = service.getAllRoutes();
            std::vector<std::string> operationIds = service.getOperationIds();
            std::set<std::string> warehouseIds;
            std::set<std::string> uniqueOperationIds(operationIds.begin(), operationIds.end());
            for (const Warehouse& warehouse : warehouses) {
                warehouseIds.insert(warehouse.id);
            }
            bool routeEndpointsValid = true;
            for (const Route& route : routes) {
                routeEndpointsValid = routeEndpointsValid &&
                                      warehouseIds.count(route.fromWarehouseId) == 1 &&
                                      warehouseIds.count(route.toWarehouseId) == 1;
            }

            const std::size_t expectedWarehouses = 3 + workerCount * 5;
            const std::size_t expectedOperations = 1 + workerCount * 10;
            const bool stateValid = workersReady && workerResultsValid && baseReady &&
                duplicateWarehouseWinners == 1 && duplicateOperationWinners == 1 &&
                warehouses.size() == expectedWarehouses &&
                warehouseIds.size() == warehouses.size() &&
                service.vertexCount() == warehouses.size() && routeEndpointsValid &&
                service.routeCount() == routes.size() &&
                operationIds.size() == expectedOperations &&
                uniqueOperationIds.size() == operationIds.size() &&
                service.getDependencies().size() == workerCount * 10 &&
                !service.hasFulfillmentCycle() &&
                service.topologicalSort().status == TopologicalSortStatus::Success;

            everyRunValid = stateValid && everyRunValid;
            ++runsCompleted;
            pool.shutdown();
        }
    }

    check(everyRunValid && runsCompleted == repetitions * 4,
          "mixed network and fulfillment stress passes at 1, 2, 4, and 8 workers");
}

void testServiceCsvImportAndExport() {
    SmartLogixService service;
    const std::string unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base = std::filesystem::temp_directory_path() /
                                       ("smartlogix_service_" + unique);
    const std::filesystem::path inputPath = base.string() + "_input.csv";
    const std::filesystem::path outputPath = base.string() + "_output.csv";
    bool filesRemoved = false;

    {
        std::ofstream input(inputPath, std::ios::binary);
        input << "id,name,location,capacity\r\n"
              << "WH-CSV,CSV Warehouse,East,25\r\n"
              << ",Invalid Warehouse,West,20\r\n";
    }

    const ImportReport imported = service.importWarehouses(inputPath.string());
    const bool partialImportPreserved = imported.status == CsvImportStatus::Partial &&
                                        imported.importedRecords == 1 &&
                                        imported.rejectedRecords == 1;
    const auto warehouse = service.searchWarehouse("WH-CSV");
    const bool graphRegistered = warehouse && service.vertexCount() == 1 &&
                                 service.getOutgoingRoutes("WH-CSV").has_value();
    const ExportReport exported = service.exportWarehouses(outputPath.string());
    std::ifstream output(outputPath, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(output)),
                               std::istreambuf_iterator<char>());
    output.close();
    const bool exportPreserved = exported.success &&
                                 contents.find("WH-CSV,CSV Warehouse,East,25") !=
                                     std::string::npos;

    std::error_code ignored;
    std::filesystem::remove(inputPath, ignored);
    const bool inputRemoved = !std::filesystem::exists(inputPath);
    std::filesystem::remove(outputPath, ignored);
    filesRemoved = inputRemoved && !std::filesystem::exists(outputPath);

    check(partialImportPreserved,
          "service CSV import preserves partial-import status and row counts");
    check(graphRegistered,
          "service CSV warehouse import registers the corresponding graph vertex");
    check(exportPreserved && filesRemoved,
          "service CSV export writes snapshot data and temporary files are removed");
}
}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    testConfiguration();
    testConcurrentTaskResults();
    testMultipleWorkersRunAtOnce();
    testQueueSaturation();
    testExceptionPropagation();
    testShutdownDrainsAcceptedWork();
    testRepeatedThreadPoolLifecycle();
    testServiceWarehouseAndGraphOperations();
    testServiceFulfillmentOperations();
    testServiceCanBeSubmittedToThreadPool();
    testConcurrentNetworkOperationsStayConsistent();
    testConcurrentFulfillmentOperations();
    testConcurrentDuplicateInsertion();
    testMixedStressAcrossWorkerCounts();
    testServiceCsvImportAndExport();

    if (failures == 0) {
        std::cout << passedChecks << "/" << checks
                  << " concurrency assertions passed\n";
        return 0;
    }
    std::cout << failures << "/" << checks << " concurrency assertions failed\n";
    return 1;
}
