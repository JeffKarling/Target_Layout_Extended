---
name: performance
description: >-
  Profiles, benchmarks and optimizes hot paths in existing components against hardware PMU baselines.
  Use this skill when the user asks to optimize, speed up, benchmark or profile an existing component.
---

# Performance optimization workflow

Use this runbook to profile hot paths, apply zero allocation optimizations and verify benchmark score improvements.

## Step 1. Measure the starting baseline

Always record the starting metrics before modifying any source code.

Before taking initial measurements, prompt the user to ensure proper hardware counter permissions and stable CPU clock frequencies.
* **Hardware PMU counters.** Verify that `/proc/sys/kernel/perf_event_paranoid` is set to 1 or lower. If set higher, recommend running `sudo sysctl kernel.perf_event_paranoid=1`.
* **Performance power profile.** Prompt the user to switch the workstation into performance mode via `powerprofilesctl set performance` or the desktop power settings to prevent CPU frequency scaling jitter.

Compile and run the component benchmark under an optimized custom release preset.

```bash
cmake --build --preset Clang_Custom_Release --target <component>BENCH_run
```

Record the starting numbers for wall clock latency, CPU cycles, retired instructions and instructions per cycle. Note that `*_Custom_Release` presets include production hardening `-fstack-protector-strong`, `_GLIBCXX_ASSERTIONS` and `_LIBCPP_HARDENING_MODE_FAST`.

## Step 2. Profile bottlenecks using hardware counters

Inspect the PMU metrics captured by `perf_scope`.
* **High instruction count.** Indicates virtual dispatch overhead, redundant copies, per-iteration bounds checks inside loops or missing standard library inlining.
* **Low IPC.** Indicates memory stalls, cache misses or branch mispredictions.
* **Branch misses.** Indicates unpredicted conditionals inside inner loops. Replace with lookup tables or arithmetic masking.
* **Cache misses.** Indicates non-contiguous memory layouts. Reorder structs for cache line locality or use `std::span`.

When deeper whole-application CPU instruction analysis is needed, run `cmake --build --preset Clang_Custom_Release --target Perf_stat`, `Perf_record` or `Perf_report` to inspect reports in `production_artifacts/profiling/perf/`.

## Step 3. Apply invariant preserving optimizations

Refactor the component implementation in `targets/<component>/<component>.cpp`.
* **Accelerated library reuse.** Inspect `cmake/dependencies.cmake` for approved accelerated libraries before writing custom vector kernels. When an approved library like Intel oneAPI MKL or OpenBLAS provides the routine, link it privately in `targets/<component>/CMakeLists.txt` rather than reinventing the algorithm.
* Replace intermediate container allocations and manual index loops with `std::views` pipelines, `std::views::enumerate` and `std::ranges::transform`.
* Slice `std::span` buffers once before a loop (`span.first(count)`) and iterate via `std::views::enumerate` or `std::ranges` algorithms so bounds checks under `_GLIBCXX_ASSERTIONS` and `_LIBCPP_HARDENING_MODE_FAST` are hoisted out of the hot loop body.
* Pass contiguous memory slices via `std::span` to enable SIMD auto-vectorization.
* Evaluate coarse compute bound loops for function multi versioning. Apply `target_clones` targeting `default`, `arch=x86-64-v3` and `arch=x86-64-v4` following `.agents/rules/function_multi_versioning_and_target_clones.md` to accelerate portable release packages.
* Hoist invariant calculations and memory allocations out of hot loops.
* Preserve public API contracts so downstream consumers remain unaffected.

## Step 4. Verify behavioral correctness under debug

Run the unit test suite across both compilers to ensure optimizations did not introduce functional defects.

```bash
./production_artifacts/build_all.sh custom debug
```

Do not proceed to benchmark evaluation if any test fails.

## Step 5. Measure after metrics under release

Recompile and execute the benchmark under optimized release.

```bash
cmake --build --preset Clang_Custom_Release --target <component>BENCH_run
```

Inspect the benchmark report written to `production_artifacts/profiling/<component>_score_report.md`.

## Step 6. Evaluate stopping criteria

Inspect the directive and bottleneck fields in the benchmark report.
* If the directive is `stop_hardware_saturated`, stop optimizing. The CPU execution units are saturated with high instructions per cycle.
* If the directive is `stop_diminishing_returns`, stop optimizing. Marginal gains for further iterations are too small to justify additional development time.
* If the directive is `regression_detected`, revert recent changes to restore the baseline performance.
* If the directive is `continue_optimizing` and optimization goals remain, address the diagnosed bottleneck and repeat the loop.

Present a concise comparison table contrasting before and after metrics in the delivery summary.
