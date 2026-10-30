---
name: performance
description: >-
  Profiles, benchmarks, and optimizes hot paths in existing components against hardware PMU baselines.
  Use this skill when the user asks to optimize, speed up, benchmark, or profile an existing component.
---

# Performance optimization workflow

Use this runbook to profile hot paths, apply zero-allocation optimizations, and verify benchmark score improvements.

## Step 1. Measure the starting baseline

Always record the starting metrics before modifying any source code.
Compile and run the target benchmark under the optimized release preset:

```bash
cmake --build Build/Clang_Custom_Release --target <component>BENCH
./Build/Clang_Custom_Release/targets/<component>/<component>BENCH/<component>BENCH
```

Record the starting numbers for wall clock latency, CPU cycles, retired instructions, and instructions per cycle.

## Step 2. Profile bottlenecks using hardware counters

Inspect the PMU metrics captured by `perf_scope`:
* **High instruction count.** Indicates virtual dispatch overhead, redundant copies, or missing standard library inlining.
* **Low IPC.** Indicates memory stalls, cache misses, or branch mispredictions.
* **Branch misses.** Indicates unpredicted conditionals inside inner loops. Replace with lookup tables or arithmetic masking.
* **Cache misses.** Indicates non-contiguous memory layouts. Reorder structs for cache line locality or use `std::span`.

When deeper CPU instruction analysis is needed, run `cmake --build Build/Clang_Custom_Release --target perf_stat` or `perf_annotate`.

## Step 3. Apply invariant preserving optimizations

Refactor the component implementation in `targets/<component>/<component>.cpp`:
* Replace intermediate container allocations with `std::views` pipelines.
* Pass contiguous memory slices via `std::span` to enable SIMD auto-vectorization.
* Hoist invariant calculations and memory allocations out of hot loops.
* Preserve public API contracts so downstream consumers remain unaffected.

## Step 4. Verify behavioral correctness under debug

Run the entire unit test suite to ensure optimizations did not introduce functional defects:

```bash
cmake --build Build/Clang_Custom_Debug
ctest --test-dir Build/Clang_Custom_Debug --output-on-failure
```

Do not proceed to benchmark evaluation if any test fails.

## Step 5. Measure after metrics under release

Recompile and execute the benchmark under optimized release:

```bash
cmake --build Build/Clang_Custom_Release --target <component>BENCH
./Build/Clang_Custom_Release/targets/<component>/<component>BENCH/<component>BENCH
```

Inspect the score report written to `production_artifacts/profiling/<component>_score_report.md`.

## Step 6. Evaluate stopping criteria

Inspect the decision field in the score report:
* If the decision is `target_score_reached`, stop optimizing. The component achieved its target performance.
* If the decision is `stop_diminishing_returns`, stop optimizing. Marginal gains for further iterations are too small to justify context tokens.
* If the decision is `continue_optimizing` and remaining token budget allows, repeat the loop.

Present a concise comparison table contrasting before and after metrics in the delivery summary.
