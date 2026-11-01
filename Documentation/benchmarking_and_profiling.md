# Benchmarking and score optimization

Monolithic performance profiling mixes algorithmic hot loops with startup routines, filesystem I/O and process teardown. Isolating the hot path in a large codebase often requires invasive source annotations such as Intel VTune ITT markers or Valgrind macros.

When software is decomposed into Lakos physical components, source instrumentation becomes unnecessary. Every component has a narrow dependency graph and a self contained interface. Its hot path can be measured in isolation through a colocated benchmark in `<name>BENCH/` using hardware performance counters from `targets/perf_scope/` and score driven regression checks in `baseline.json`.

## Colocated component benchmarks

Just as `<name>TEST/` isolates unit verification, `<name>BENCH/` isolates performance measurement.

```text
targets/tbb_algos/
├── CMakeLists.txt
├── tbb_algos.cpp
├── tbb_algos.catalog
├── tbb_algos/
│   └── tbb_algos.hpp
├── tbb_algosTEST/
│   ├── CMakeLists.txt
│   └── tbb_algosTEST.cpp
└── tbb_algosBENCH/
    ├── CMakeLists.txt
    ├── tbb_algosBENCH.cpp
    └── baseline.json
```

The benchmark `CMakeLists.txt` derives target names from `${DIR_NAME}` and wires up baseline paths and run targets.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)
string(REGEX REPLACE "BENCH$" "" COMPONENT_NAME ${DIR_NAME})

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_compile_definitions(${DIR_NAME} PRIVATE
        COMPONENT_BASELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/baseline.json"
        COMPONENT_REPORT_PATH="${PROJECT_SOURCE_DIR}/production_artifacts/profiling/${COMPONENT_NAME}_score_report.md"
)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::${COMPONENT_NAME}
        ${PROJECT_NAME}::perf_scope
)

add_test(NAME ${DIR_NAME} COMMAND ${DIR_NAME})

add_custom_target(${DIR_NAME}_run
        COMMAND $<TARGET_FILE:${DIR_NAME}>
        DEPENDS ${DIR_NAME}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running component benchmark ${DIR_NAME} and evaluating against baseline.json"
        VERBATIM
)
```

Because `tbb_algosBENCH` links only `tbb_algos` and `perf_scope`, the benchmark binary runs without application startup overhead. Measurement happens entirely inside the harness, leaving production component sources free of profiler hooks.

Notice that the benchmark binary is also registered with `add_test()`. Running `ctest` executes the benchmark as a regression verification step.

## Hardware counters in perf_scope

The `targets/perf_scope/` component wraps `<linux/perf_event.h>` using the `perf_event_open` system call inside an RAII guard named `perf_scope::counter_guard`.

When `perf_scope::measure_workload()` executes a workload, it runs a warmup iteration to prime instruction and data caches. It resets and enables the hardware PMU counter group immediately before the timed loop, and disables the counter group when the loop finishes.

It captures core timing and hardware metrics per operation.

* **Wall clock nanoseconds per operation.** Recorded in `wall_ns_per_op` through `std::chrono::steady_clock`.
* **CPU cycles per operation.** Recorded in `cycles_per_op` from `PERF_COUNT_HW_CPU_CYCLES`.
* **Retired instructions per operation.** Recorded in `instructions_per_op` from `PERF_COUNT_HW_INSTRUCTIONS`.
* **Instructions per cycle.** Recorded in `ipc` as the ratio of retired instructions to CPU cycles.
* **Branch and cache misses.** Recorded in `branch_misses_per_op` and `cache_misses_per_op` from `PERF_COUNT_HW_BRANCH_MISSES` and `PERF_COUNT_HW_CACHE_MISSES`.

### Kernel permissions and fallback timing

On Linux systems where `/proc/sys/kernel/perf_event_paranoid` is set above 1, unprivileged processes cannot open hardware PMU counters. In that scenario `perf_scope` logs an operational warning and falls back to `std::chrono::steady_clock` timing without failing the benchmark run or test suite.

To allow unprivileged hardware counter access on developer workstations, set the sysctl value.

```bash
sudo sysctl kernel.perf_event_paranoid=1
```

## Birth baseline and score driven optimization

Unbounded optimization consumes engineering effort when a component is already fast enough for its purpose. To keep optimization bounded, each `<name>BENCH/` target stores a birth baseline in `baseline.json`.

```json
{
  "component": "tbb_algos",
  "human_baseline_score": 6.000,
  "target_score": 8.500,
  "token_budget": 4000,
  "utility_curve": "logarithmic",
  "wall_ns_per_op": 650000.000,
  "cycles_per_op": 1850000.000,
  "instructions_per_op": 3900000.000,
  "ipc": 2.108,
  "branch_misses_per_op": 450.000,
  "cache_misses_per_op": 120.000
}
```

### Utility curves and stop conditions

The evaluation engine computes a speedup factor by comparing current `wall_ns_per_op` with the birth baseline. That speedup maps to a delta score using the chosen utility curve.

* **Logarithmic curve.** Awards 2.0 score points for each doubling of performance via $\Delta = 2.0 \cdot \log_2(\text{speedup})$. Early gains score well while extreme micro optimizations yield diminishing returns.
* **Diminishing returns curve.** Applies a steeper square root decay for components where only initial algorithmic wins matter.
* **Linear curve.** Scales score points proportionally with speedup. Useful on core bottleneck components where every percentage gain translates directly to system throughput.

Optimization halts when current score reaches `target_score`, or when the projected score gain for another 20 percent speedup drops below 0.25 points.

### Baseline creation on first run

When a new benchmark runs for the first time without an existing `baseline.json`, `perf_scope::load_or_create_baseline()` measures the initial hardware counters and serializes `baseline.json` automatically using the `score_baseline` struct provided by `main()`.

```cpp
ps::score_baseline default_birth{};
default_birth.component = "tbb_algos";
default_birth.human_baseline_score = 6.0;
default_birth.target_score = 8.5;
default_birth.token_budget = 4000U;
default_birth.curve = ps::utility_curve::logarithmic;
default_birth.birth_metrics = current_metrics;
```

Commit `baseline.json` alongside the benchmark source code. Future benchmark runs compare against this committed snapshot.

### Executing benchmarks and reading reports

Run the dedicated CMake benchmark target to measure the workload and print the score report.

```bash
cmake --build --preset GNU_Custom_Release --target tbb_algosBENCH_run
```

```markdown
# Component Benchmark & Score Report (tbb_algos)
- Birth baseline score: 6.00 / 10.00 (target: 8.50, curve: logarithmic, token budget: 4000)
- Latency (ns/op):      318450.00 (birth: 650000.00, speedup: 2.04x)
- Hardware PMU/op:      cycles=910200.00, instructions=2450100.00, IPC=2.69, branch_misses=180.00
- Achieved score:       8.06 / 10.00 (delta: +2.06)
- Workflow decision:    continue_optimizing (marginal gain for +20% speedup: +0.53 pts)
```

The report is written to stdout and saved to `production_artifacts/profiling/<name>_score_report.md`.

## Whole application profiling with Linux perf

When you need to profile the entire application across all components or inspect disassembled assembly instructions, `cmake/perf.cmake` provides CMake targets that write reports to `production_artifacts/profiling/perf/`.

* **`Perf_stat`.** Runs `perf stat` across hardware and scheduling counters and writes `perf_stat_report.txt`.
* **`Perf_record`.** Records DWARF call graph samples with `perf record -g --call-graph dwarf -F 999` to `perf.data`.
* **`Perf_report`.** Runs `perf report --stdio --no-children` to generate a hotspot summary table in `perf_hotspot_report.txt`.
* **`Perf_annotate`.** Runs `perf annotate --stdio -l` to write instruction level cycle attribution to `perf_annotate_report.txt`.
