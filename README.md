# SmartLogix Submission

SmartLogix is a C++17 warehouse and logistics application with a custom hash table, a weighted transportation graph, a fulfillment dependency graph, CSV import/export, and a bounded worker pool for concurrent service requests.

## Contents

- `Source_Code/` contains the application and benchmark implementation.
- `Tests/` contains the seven existing test suites and the dedicated concurrency suite.
- `Benchmark_Results/concurrent_benchmark_results.csv` contains the measured concurrent benchmark output (12 workload/worker combinations).
- `Documentation/` contains the architecture and performance reports.

## Build requirements

- Windows with MSYS2 UCRT64 GCC 16.2.0 (or a compatible C++17 GCC toolchain).
- PowerShell. GCC's runtime directory must be on the process PATH when running the generated executables.
- No third-party libraries are required.

Open PowerShell in this submission directory. The following commands build the CLI, benchmark runner, all seven regular suites, and concurrency suite into `.build/`:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
New-Item -ItemType Directory -Force .build | Out-Null
$src = "Source_Code"
$common = @(
  "$src\algorithms.cpp", "$src\benchmark.cpp", "$src\csv_manager.cpp",
  "$src\fulfillment.cpp", "$src\graph.cpp", "$src\hash_table.cpp",
  "$src\warehouse_manager.cpp", "$src\smartlogix_service.cpp",
  "$src\thread_pool.cpp"
)

& $gxx -std=c++17 -pthread "$src\main.cpp" "$src\smartlogix_service.cpp" `
  "$src\csv_manager.cpp" "$src\fulfillment.cpp" "$src\graph.cpp" `
  "$src\algorithms.cpp" "$src\warehouse_manager.cpp" "$src\hash_table.cpp" `
  -o .build\SmartLogix.exe
& $gxx -std=c++17 -O2 -pthread "$src\benchmark_main.cpp" "$src\benchmark.cpp" `
  "$src\smartlogix_service.cpp" "$src\thread_pool.cpp" "$src\algorithms.cpp" `
  "$src\csv_manager.cpp" "$src\fulfillment.cpp" "$src\graph.cpp" `
  "$src\hash_table.cpp" "$src\warehouse_manager.cpp" -o .build\SmartLogixBenchmark.exe

foreach ($suite in @("algorithms", "benchmark", "csv_manager", "fulfillment", `
                     "graph", "hash_table", "warehouse_manager", "concurrency")) {
  & $gxx -std=c++17 -O2 -pthread "Tests\${suite}_tests.cpp" @common `
    -o ".build\${suite}_tests.exe"
  if ($LASTEXITCODE -ne 0) { throw "Build failed for $suite" }
}
```

Run the tests:

This builds and runs `Tests\concurrency_tests.cpp` alongside the seven regular test suites.

```powershell
foreach ($suite in @("algorithms", "benchmark", "csv_manager", "fulfillment", `
                     "graph", "hash_table", "warehouse_manager", "concurrency")) {
  & ".build\${suite}_tests.exe"
  if ($LASTEXITCODE -ne 0) { throw "Tests failed for $suite" }
}
```

Run the interactive application and benchmark tools:

```powershell
.\.build\SmartLogix.exe
.\.build\SmartLogixBenchmark.exe .build\serial_results.csv
.\.build\SmartLogixBenchmark.exe .build\concurrent_results.csv --concurrent
```

Append `--smoke` to a benchmark command for its small deterministic smoke profile. Benchmark timings depend on the host and should be measured again on the target machine.
