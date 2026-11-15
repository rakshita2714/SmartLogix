# SmartLogix

[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![GCC 16.2.0](https://img.shields.io/badge/GCC-16.2.0-A42E2B?style=flat&logo=gnu)]
![Graph Algorithms](https://img.shields.io/badge/Algorithms-Dijkstra%20%7C%20BFS%20%7C%20DFS-6f42c1?style=flat)
![Concurrency](https://img.shields.io/badge/Concurrency-C%2B%2B%20Threads-138A8A?style=flat)

**SmartLogix** is a C++17 logistics supply-chain and fulfillment optimization system for managing warehouses, directed transportation routes, and fulfillment dependencies. It combines custom data structures and graph algorithms with CSV persistence, a bounded thread pool, and repeatable performance benchmarks.

This B.Tech DAA project demonstrates algorithm design and a service boundary for concurrent requests. The interactive CLI remains synchronous; concurrent requests are exercised through the service API and benchmark workloads.

## Table of Contents
- [Overview](#overview)
- [Key Features](#key-features)
- [Architecture](#architecture)
- [Data Structures and Algorithms](#data-structures-and-algorithms)
- [Concurrency Model](#concurrency-model)
- [Performance Results](#performance-results)
- [Testing](#testing)
- [Build and Run](#build-and-run)
- [Repository Structure](#repository-structure)
- [Limitations and Future Improvements](#limitations-and-future-improvements)
- [Reports and Benchmark Data](#reports-and-benchmark-data)

## Overview

SmartLogix maintains warehouse records, directed weighted routes, and fulfillment operations with prerequisite dependencies. CSV import/export supports data exchange. <code>SmartLogixService</code> coordinates changes that need to keep warehouse and transportation graph state consistent.

## Key Features

- Custom separate-chaining hash table with load-factor-based resizing.
- Warehouse creation, lookup, update, deletion, and listing.
- Weighted transportation graph with Dijkstra shortest path, BFS, and DFS.
- Fulfillment dependency graph with cycle detection and topological sorting.
- CSV import/export with row-level reporting and rollback behavior.
- Fixed-size <code>std::thread</code> pool with bounded queue, futures, exception propagation, and orderly shutdown.
- Service-level shared/exclusive synchronization for network and fulfillment domains.
- Serial and concurrent benchmarks reporting throughput and latency percentiles.

## Architecture

The CLI calls <code>SmartLogixService</code> synchronously. The concurrent benchmark submits service requests to <code>ThreadPool</code>. The service owns domain components and coordinates their locks. <code>CsvManager</code> parses and writes CSV through service-managed operations.

~~~mermaid
flowchart LR
    CLI["CLI<br/>(synchronous)"] --> Service["SmartLogixService"]
    Benchmark["BenchmarkRunner"] --> Pool["ThreadPool<br/>fixed workers<br/>bounded queue"]
    Pool --> Service
    subgraph Network["Network domain: shared_mutex + writer gate"]
        Warehouses["WarehouseManager<br/>records + ID index"]
        Transport["TransportationGraph<br/>Dijkstra / BFS / DFS"]
        Warehouses <--> Transport
    end
    subgraph FulfillmentDomain["Fulfillment domain: shared_mutex + writer gate"]
        Fulfillment["FulfillmentGraph<br/>cycle detection / topological sort"]
    end
    Service --> Network
    Service --> FulfillmentDomain
    Service --> Csv["CsvManager<br/>parse / validate / export"]
    Csv -. "service-coordinated updates and snapshots" .-> Network
    Csv -. "service-coordinated updates and snapshots" .-> FulfillmentDomain
~~~

| Component | Responsibility |
|---|---|
| <code>main.cpp</code> | Validates input, displays the existing menu, and routes CLI operations through the service. |
| <code>SmartLogixService</code> | Owns domain managers, coordinates synchronization and consistency, and returns copied values/results. |
| <code>WarehouseManager</code> | Stores warehouse records and maintains the ID-to-index mapping. |
| <code>TransportationGraph</code> | Stores directed weighted routes in adjacency lists and maintains a vertex index. |
| <code>GraphAlgorithms</code> | Implements shortest path and graph traversals. |
| <code>FulfillmentGraph</code> | Stores operation dependencies and provides cycle detection and topological ordering. |
| <code>CsvManager</code> | Parses, validates, imports, and exports domain data. |
| <code>ThreadPool</code> | Runs tasks on fixed workers and bounds the waiting queue. |
| <code>BenchmarkRunner</code> | Creates deterministic workloads, gathers metrics, and writes CSV results. |

## Data Structures and Algorithms

The following are theoretical bounds. Hash-table expected complexity assumes reasonably distributed keys. Let <code>n</code> be item count, <code>V</code> vertices, <code>E</code> edges, and <code>deg(u)</code> the outgoing degree of vertex <code>u</code>.

| Component | Data structure | Operation | Time complexity | Purpose |
|---|---|---|---|---|
| <code>HashTable</code> | Bucket vector with separate-chaining lists | Search, insert, remove | Expected O(1); worst O(n) | Maps string IDs to indices. |
| <code>HashTable</code> | Same | Resize and rehash | O(n) | Redistributes entries after the load threshold. |
| <code>WarehouseManager</code> | Warehouse vector and hash index | Lookup / add | Expected O(1) index lookup; worst O(n) | Stores records and resolves IDs. |
| <code>WarehouseManager</code> | Vector and hash index | Delete and repair indices | O(n) | Repairs indices shifted by vector erase. |
| <code>TransportationGraph</code> | Adjacency vectors and vertex hash index | Find/add/remove outgoing route | O(deg(u)) scan plus expected O(1) ID lookup | Maintains directed weighted connections. |
| <code>GraphAlgorithms</code> | Adjacency lists and priority queue | Dijkstra shortest path | O((V + E) log V), binary heap | Finds shortest path for non-negative weights. |
| <code>GraphAlgorithms</code> | Adjacency lists and visited state | BFS / DFS | O(V + E) | Traverses reachable warehouses. |
| <code>FulfillmentGraph</code> | Operation vector, dependent-index lists, hash index | Add operation / dependency | Expected O(1) ID lookup; duplicate scan O(deg(u)) | Represents prerequisites. |
| <code>FulfillmentGraph</code> | Same | Cycle check / Kahn topological sort | O(V + E) | Detects cycles and orders valid work. |
| <code>CsvManager</code> | Parsed rows and copied records | Import / export | O(bytes + records + edges), excluding file I/O | Exchanges validated data. |
| <code>ThreadPool</code> | Worker vector and bounded task deque | Enqueue / dequeue | O(1) queue operation under mutex | Schedules concurrent requests. |

## Concurrency Model

Concurrency is coordinated at the service/request layer rather than inside every container.

- **Fixed worker pool:** configured <code>std::thread</code> workers consume tasks from a shared queue.
- **Bounded queue and backpressure:** a full waiting queue rejects submission with <code>QueueFull</code>.
- **Futures and exceptions:** <code>submit()</code> returns a <code>std::future</code>; task exceptions are rethrown by <code>future.get()</code>.
- **Graceful shutdown:** shutdown rejects new work, drains accepted tasks, and joins workers.
- **Network domain:** one <code>std::shared_mutex</code> protects warehouse and transportation operations together. Reads take shared locks; mutations take exclusive locks.
- **Fulfillment domain:** a separate <code>std::shared_mutex</code> protects fulfillment reads and mutations.
- **Writer gates:** a mutex per domain serializes writers while permitting concurrent readers.
- **Value-returning service API:** lookups and collections return copies instead of internal pointers, references, or iterators.
- **CSV consistency:** imports apply changes under the relevant exclusive service lock. Exports copy snapshots under a shared lock, then write after releasing it.

Use <code>SmartLogixService</code> as the synchronization boundary for concurrent access. The interactive CLI processes one menu action at a time.

## Performance Results

Values below come from [<code>concurrent_benchmark_results.csv</code>](Benchmark_Results/concurrent_benchmark_results.csv). Each workload/worker pair used a deterministic 256-entity dataset and 1,280 completed operations over five repetitions. All configurations recorded zero failed operations. Latencies are microseconds; throughput is operations per second.

| Workload | Workers | Throughput (ops/s) | p50 (us) | p95 (us) | p99 (us) |
|---|---:|---:|---:|---:|---:|
| Read-heavy | 1 | 64,286.40 | 1,934.20 | 4,100.20 | 4,868.10 |
| Read-heavy | 2 | 121,252.31 | 1,008.40 | 1,813.10 | 1,941.70 |
| Read-heavy | 4 | 137,037.63 | 851.90 | 1,807.80 | 2,121.60 |
| Read-heavy | 8 | 182,338.78 | 639.30 | 1,121.30 | 1,219.30 |
| Write-heavy | 1 | 246,970.75 | 423.20 | 758.60 | 918.10 |
| Write-heavy | 2 | 235,285.47 | 464.80 | 828.10 | 958.50 |
| Write-heavy | 4 | 213,003.18 | 546.10 | 967.40 | 1,054.00 |
| Write-heavy | 8 | 194,988.19 | 597.50 | 1,037.10 | 1,090.20 |
| Mixed | 1 | 204,512.05 | 548.50 | 1,251.90 | 1,462.40 |
| Mixed | 2 | 170,403.11 | 694.70 | 1,301.50 | 1,616.10 |
| Mixed | 4 | 205,930.14 | 550.00 | 966.20 | 1,041.80 |
| Mixed | 8 | 204,039.34 | 497.90 | 948.00 | 1,035.20 |

- **Read-heavy:** highest throughput was at 8 workers; p50/p95/p99 were also lower at 8 than at 1 worker.
- **Write-heavy:** highest throughput was at 1 worker and declined as workers increased, consistent with exclusive-write synchronization overhead.
- **Mixed:** 4 workers had the highest throughput, only slightly above 1 worker. Scaling was not monotonic.
- Results describe this run and configuration; they do not guarantee gains on larger datasets or other hardware.

## Testing

The repository has seven existing suites and a separate concurrency suite.

| Suite | Coverage |
|---|---|
| <code>algorithms_tests.cpp</code> | Dijkstra, BFS, DFS, and edge cases. |
| <code>benchmark_tests.cpp</code> | Serial benchmark behavior, CSV output, and concurrent workload metrics. |
| <code>csv_manager_tests.cpp</code> | Validation, partial imports, rollback, and round trips. |
| <code>fulfillment_tests.cpp</code> | Dependency validation, cycle detection, and topological ordering. |
| <code>graph_tests.cpp</code> | Routes, vertices, and warehouse/graph consistency. |
| <code>hash_table_tests.cpp</code> | Collisions, updates/removal, resizing, invalid capacities. |
| <code>warehouse_manager_tests.cpp</code> | Validation, lookup, update, deletion, and index repair. |
| <code>concurrency_tests.cpp</code> | ThreadPool lifecycle/backpressure and concurrent service reads/writes. |

Verified status: **72/72 regression test groups**, **33/33 concurrency assertions**, and **7/7 benchmark test groups**. These are test counts; benchmark rows and worker configurations are separate measurements.

## Build and Run

### Prerequisites

- Windows with PowerShell.
- MSYS2 UCRT64 GCC 16.2.0 (or a compatible C++17 GCC).
- No third-party libraries or package downloads.

Open PowerShell in the repository root (`D:\SmartLogix_GitHub`). The PATH change below applies only to that PowerShell process.

### Build the application and benchmark

~~~powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$src = "Source_Code"
New-Item -ItemType Directory -Force .build | Out-Null

$appSources = @(
  "$src\main.cpp", "$src\smartlogix_service.cpp", "$src\csv_manager.cpp",
  "$src\fulfillment.cpp", "$src\graph.cpp", "$src\algorithms.cpp",
  "$src\warehouse_manager.cpp", "$src\hash_table.cpp"
)
& $gxx -std=c++17 -pthread @appSources -o .build\SmartLogix.exe
if ($LASTEXITCODE -ne 0) { throw "SmartLogix build failed" }

$benchmarkSources = @(
  "$src\benchmark_main.cpp", "$src\benchmark.cpp",
  "$src\smartlogix_service.cpp", "$src\thread_pool.cpp", "$src\algorithms.cpp",
  "$src\csv_manager.cpp", "$src\fulfillment.cpp", "$src\graph.cpp",
  "$src\hash_table.cpp", "$src\warehouse_manager.cpp"
)
& $gxx -std=c++17 -O2 -pthread @benchmarkSources -o .build\SmartLogixBenchmark.exe
if ($LASTEXITCODE -ne 0) { throw "Benchmark build failed" }
~~~

### Build all test suites

Each test is linked with the implementation units. Run from the repository root.

~~~powershell
$common = @(
  "$src\algorithms.cpp", "$src\benchmark.cpp", "$src\csv_manager.cpp",
  "$src\fulfillment.cpp", "$src\graph.cpp", "$src\hash_table.cpp",
  "$src\warehouse_manager.cpp", "$src\smartlogix_service.cpp",
  "$src\thread_pool.cpp"
)
$suites = @(
  "algorithms", "benchmark", "csv_manager", "fulfillment",
  "graph", "hash_table", "warehouse_manager", "concurrency"
)
foreach ($suite in $suites) {
  $testSource = "Tests\" + $suite + "_tests.cpp"
  $testOutput = ".build\" + $suite + "_tests.exe"
  & $gxx -std=c++17 -O2 -pthread $testSource @common -o $testOutput
  if ($LASTEXITCODE -ne 0) { throw "Build failed for $suite" }
}
~~~

### Run all tests

~~~powershell
foreach ($suite in $suites) {
  & (".build\" + $suite + "_tests.exe")
  if ($LASTEXITCODE -ne 0) { throw "Tests failed for $suite" }
}
~~~

### Run the application and benchmarks

~~~powershell
& ".build\SmartLogix.exe"

# Serial benchmark
& ".build\SmartLogixBenchmark.exe" ".build\serial_results.csv"

# Concurrent benchmark with 1, 2, 4, and 8 workers
& ".build\SmartLogixBenchmark.exe" ".build\concurrent_results.csv" --concurrent
~~~

Append <code>--smoke</code> for the smaller deterministic smoke profile. Each benchmark writes CSV to the given output path.

## Repository Structure

~~~text
SmartLogix_GitHub/
|-- .gitignore
|-- README.md
|-- Documentation/
|   |-- SmartLogix_Performance_Analysis.pdf
|   +-- SmartLogix_System_Architecture.pdf
|-- Source_Code/
|   |-- algorithms.cpp, algorithms.h
|   |-- benchmark.cpp, benchmark.h, benchmark_main.cpp
|   |-- csv_manager.cpp, csv_manager.h
|   |-- fulfillment.cpp, fulfillment.h
|   |-- graph.cpp, graph.h
|   |-- hash_table.cpp, hash_table.h
|   |-- main.cpp
|   |-- smartlogix_service.cpp, smartlogix_service.h
|   |-- thread_pool.cpp, thread_pool.h
|   |-- warehouse.h
|   +-- warehouse_manager.cpp, warehouse_manager.h
|-- Tests/
|   |-- algorithms_tests.cpp
|   |-- benchmark_tests.cpp
|   |-- concurrency_tests.cpp
|   |-- csv_manager_tests.cpp
|   |-- fulfillment_tests.cpp
|   |-- graph_tests.cpp
|   |-- hash_table_tests.cpp
|   +-- warehouse_manager_tests.cpp
+-- Benchmark_Results/
    +-- concurrent_benchmark_results.csv
~~~

## Limitations and Future Improvements

- The concurrent benchmark uses a small deterministic dataset (256 entities) and five repetitions. It compares these workloads but does not predict production capacity.
- CPU model, core count, memory, storage, and Windows build were not recorded, limiting cross-machine reproducibility.
- Long graph queries hold a shared network lock and can delay writers in that domain.
- The CLI is synchronous. There is no network server, REST API, or distributed processing.
- The service API is the synchronization boundary. Direct concurrent use of legacy <code>WarehouseManager</code>, <code>TransportationGraph</code>, or <code>FulfillmentGraph</code> instances bypasses service locking because these components do not synchronize themselves.
- Larger benchmark profiles, host metadata capture, and lock-wait measurements are possible future improvements; they are not implemented.

## Reports and Benchmark Data

- [System Architecture](Documentation/SmartLogix_System_Architecture.pdf)
- [Performance Analysis](Documentation/SmartLogix_Performance_Analysis.pdf)
- [Concurrent benchmark CSV](Benchmark_Results/concurrent_benchmark_results.csv)

