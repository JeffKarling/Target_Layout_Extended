# Benchmarking and score optimization

Profiling an entire application binary mixes hot algorithmic loops with startup overhead, file I/O, and serialization noise. Historically, filtering out that noise required instrumenting production source code with vendor macros such as Intel ITT `__itt_pause()` and `__itt_resume()`.

**Target Layout Extended** replaces whole binary vendor instrumentation with **colocated component benchmarks** (`<name>BENCH/`), **Linux kernel hardware counters** (`targets/perf_scope/`), and **score driven baseline evaluation** (`baseline.json`).

## 1. Colocated component benchmarks (`<name>BENCH/`)

Just as `<name>TEST/` verifies a component's correctness in isolation, `<name>BENCH/` measures a component's physical performance in isolation.

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

The benchmark `CMakeLists.txt` follows the exact same `${DIR_NAME}` derivation rule as unit tests.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)
string(REGEX REPLACE "BENCH$" "" COMPONENT_NAME ${DIR_NAME})

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::${COMPONENT_NAME}
        ${PROJECT_NAME}::perf_scope
)
```

Because `tbb_algosBENCH` links only `tbb_algos` and `perf_scope`, the binary contains zero database setup or application clutter. And because the production code in `tbb_algos.cpp` contains zero profiler macros, component sources stay completely clean.

## 2. Zero noise hardware counters via `targets/perf_scope/`

The `targets/perf_scope/` component wraps the native Linux kernel `<linux/perf_event.h>` subsystem (`perf_event_open` system call) inside an RAII guard (`perf_scope::counter_guard`).

When `perf_scope::measure_workload()` runs a component function, it executes one warmup pass to prime instruction and data caches, resets and enables the hardware PMU counter group immediately before the timed loop, and disables the counter group the instant the loop finishes.

It captures five physical metrics per operation.

* **Wall clock nanoseconds per operation (`wall_ns_per_op`).** Measured via `std::chrono::steady_clock`.
* **CPU cycles per operation (`cycles_per_op`).** Hardware counter `PERF_COUNT_HW_CPU_CYCLES`.
* **Retired instructions per operation (`instructions_per_op`).** Hardware counter `PERF_COUNT_HW_INSTRUCTIONS`.
* **Instructions per cycle (`ipc`).** Derived ratio indicating instruction level parallelism versus pipeline stalls.
* **Branch and cache misses (`branch_misses_per_op` and `cache_misses_per_op`).** Hardware counters `PERF_COUNT_HW_BRANCH_MISSES` and `PERF_COUNT_HW_CACHE_MISSES`.

If the host Linux kernel restricts hardware PMU access (`perf_event_paranoid > 1`), `perf_scope` automatically falls back to high resolution `steady_clock` timing without failing the build or test suite.

## 3. Birth baseline and score driven optimization (`baseline.json`)

Unbounded performance optimization wastes engineering time and AI agent context tokens when a component is already fast enough. To make performance optimization self driving and bounded, each `<name>BENCH/` directory stores a **component birth baseline** in `baseline.json`.

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

### How the human orchestrator configures the baseline

When a component is first created, the benchmark records its birth physical metrics. A human developer reviews those numbers and sets four fields in `baseline.json`.

* **`human_baseline_score`.** Where the human rates the component's initial birth performance on a 1 to 10 scale.
* **`target_score`.** The score on the 1 to 10 scale where optimization should stop.
* **`token_budget`.** Advisory token budget allocated for an automated agent optimization loop.
* **`utility_curve`.** Defines how physical speedup translates into score improvements (`logarithmic`, `diminishing_returns`, or `linear`).

Under the `logarithmic` and `diminishing_returns` curves, moving a slow component from score 5 to score 7 yields high reward, whereas pushing from 8 to 10 yields smaller marginal score gains.

### Low token feedback loop for AI agents

Running the benchmark target executes the workload, compares current metrics against `baseline.json`, and writes a compact 6 line report to both the console and `production_artifacts/profiling/<name>_score_report.md`.

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

Once the achieved score reaches `target_score`, or when the marginal score gain drops below the diminishing returns threshold, the report outputs `target_score_reached` or `stop_diminishing_returns`, signaling the optimization loop to stop.

## 4. Whole application Linux `perf` reports

When you want to profile the full `app` executable across all components or inspect assembly instructions, `cmake/perf.cmake` provides four non interactive CMake targets that write plain text reports to `production_artifacts/profiling/perf/`.

* **`Perf_stat`.** Runs `perf stat` across hardware and scheduling counters and saves `perf_stat_report.txt`.
* **`Perf_record`.** Records DWARF call graph samples (`perf record -g --call-graph dwarf -F 999`) to `perf.data`.
* **`Perf_report`.** Runs `perf report --stdio --no-children` to generate a plain text hotspot table in `perf_hotspot_report.txt` without blocking on an interactive terminal UI.
* **`Perf_annotate`.** Runs `perf annotate --stdio -l` to write instruction level and source line cycle attribution to `perf_annotate_report.txt`.
