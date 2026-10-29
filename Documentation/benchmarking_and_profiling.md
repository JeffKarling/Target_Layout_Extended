# Benchmarking and score optimization

Profiling an entire application binary mixes algorithmic hot loops with startup overhead, file I/O, and serialization work. In monolithic projects where components are large or tightly coupled, filtering out that background activity often requires instrumenting production source code with vendor specific profiler annotations such as Intel VTune ITT markers, NVIDIA NVTX ranges, AMD uProf user markers, or Valgrind Callgrind macros.

When a project decomposes software into small, focused physical components, source code instrumentation becomes unnecessary. Because each component has a narrow dependency graph and a self contained interface, its hot path can be exercised directly by a colocated benchmark executable in `<name>BENCH/` using Linux kernel hardware counters in `targets/perf_scope/` and score driven evaluation in `baseline.json`.

## Colocated component benchmarks

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

The benchmark `CMakeLists.txt` follows the same `${DIR_NAME}` derivation rule as unit tests.

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

Because `tbb_algosBENCH` links only `tbb_algos` and `perf_scope`, the benchmark binary avoids database setup and application startup clutter. And because measurement happens inside the benchmark harness rather than inside `tbb_algos.cpp`, production component sources remain free of profiler macros.

## Isolated hardware counters in perf_scope

The `targets/perf_scope/` component wraps the Linux kernel `<linux/perf_event.h>` subsystem via the `perf_event_open` system call inside an RAII guard named `perf_scope::counter_guard`.

When `perf_scope::measure_workload()` runs a component function, it executes a warmup pass to prime instruction and data caches, resets and enables the hardware PMU counter group immediately before the timed loop, and disables the counter group as soon as the loop finishes.

It captures core timing and hardware metrics per operation.

* **Wall clock nanoseconds per operation.** Recorded in `wall_ns_per_op` via `std::chrono::steady_clock`.
* **CPU cycles per operation.** Recorded in `cycles_per_op` from `PERF_COUNT_HW_CPU_CYCLES`.
* **Retired instructions per operation.** Recorded in `instructions_per_op` from `PERF_COUNT_HW_INSTRUCTIONS`.
* **Instructions per cycle.** Recorded in `ipc` as the ratio of retired instructions to CPU cycles.
* **Branch and cache misses.** Recorded in `branch_misses_per_op` and `cache_misses_per_op` from `PERF_COUNT_HW_BRANCH_MISSES` and `PERF_COUNT_HW_CACHE_MISSES`.

If the host Linux kernel restricts hardware PMU access when `perf_event_paranoid > 1`, `perf_scope` falls back to `steady_clock` timing without failing the build or test suite.

## Birth baseline and score driven optimization

Unbounded performance optimization wastes engineering time and AI agent context tokens when a component is already fast enough for its role. To keep optimization bounded, each `<name>BENCH/` directory stores a component birth baseline in `baseline.json`.

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

### How default score, curve, and budget values are defined

At the library level, `perf_scope::score_baseline` defines conservative fallback defaults in `perf_scope/perf_scope.hpp`.

* **`human_baseline_score` defaults to `5.0`.** Places an unreviewed component at the midpoint of the 1.0 to 10.0 scale.
* **`target_score` defaults to `8.0`.** Requires a meaningful speedup before signaling that optimization can stop.
* **`token_budget` defaults to `4000`.** Caps automated agent optimization effort to a couple of focused passes.
* **`utility_curve` defaults to `logarithmic`.** Awards $+2.0$ score points for each $2\times$ speedup via $\Delta = 2.0 \log_2(\text{speedup})$ so early gains score well while extreme micro optimizations taper off. The evaluator also supports `diminishing_returns` for steeper early tapering and `linear` where every speedup increment scales evenly.

### Agent baseline estimation versus human review

When an AI agent creates a new component and its `<name>BENCH/` target, `perf_scope::load_or_create_baseline()` measures the component's initial hardware counters on first run and writes `baseline.json`. Rather than leaving policy fields empty, the agent populates initial values in `<name>BENCH.cpp` by evaluating the implementation structure alongside the measured hardware counters.

* **Initial score estimation.** If the birth implementation uses naive scalar loops, frequent heap allocations, or exhibits low `ipc` and high cache or branch misses, the agent estimates a lower starting `human_baseline_score` around `4.0` to `5.0` because substantial optimization headroom exists. If the component already uses parallel primitives and contiguous memory with high instruction throughput, such as `tbb_algos` starting with `oneapi::tbb::parallel_for_each` and `ipc` above `2.0`, the agent sets a higher initial score such as `6.0`.
* **Curve and target selection.** For general compute components, the agent selects `logarithmic` and a `target_score` between `8.0` and `8.5` with a `4000` token budget.

Understanding this estimation logic helps a human developer decide when to trust the agent's defaults and when to intervene.

* **When to trust the defaults.** If the component is a standard workload and the agent's hardware counter assessment matches expectations, the developer can leave `baseline.json` untouched. The default curve and budget prevent runaway optimization while still capturing straightforward algorithmic speedups.
* **When to override `baseline.json`.** Because the agent only sees local hardware counters and code structure, it does not always know business criticality. If a human knows a component sits on a system bottleneck where even a $15\%$ gain matters, the human should not rely on the default stop threshold. Instead, the developer can lower `human_baseline_score`, raise `target_score`, switch `utility_curve` to `linear`, or raise `token_budget` to direct deeper loop optimization. Conversely, if the component runs once at startup, the human can set `human_baseline_score` equal to `target_score` so no optimization effort is spent.

### Compact feedback loop for optimization runs

Running the benchmark target executes the workload, compares current metrics against `baseline.json`, and writes a markdown report to both the console and `production_artifacts/profiling/<name>_score_report.md`.

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

Once the achieved score reaches `target_score`, or when the projected score gain for another $20\%$ speedup drops below `0.25` points, the report outputs `target_score_reached` or `stop_diminishing_returns`.

## Whole application Linux perf reports

When you want to profile the full `app` executable across all components or inspect assembly instructions, `cmake/perf.cmake` provides non interactive CMake targets that write plain text reports to `production_artifacts/profiling/perf/`.

* **`Perf_stat`.** Runs `perf stat` across hardware and scheduling counters and saves `perf_stat_report.txt`.
* **`Perf_record`.** Records DWARF call graph samples via `perf record -g --call-graph dwarf -F 999` to `perf.data`.
* **`Perf_report`.** Runs `perf report --stdio --no-children` to generate a plain text hotspot table in `perf_hotspot_report.txt` without blocking on an interactive terminal UI.
* **`Perf_annotate`.** Runs `perf annotate --stdio -l` to write instruction level and source line cycle attribution to `perf_annotate_report.txt`.
