# Mandatory benchmark verification on code changes

## Core requirement

Every algorithmic modification, hot path refactoring, data structure update, or interface redesign must include benchmark execution and regression verification. Agents must actively prevent latency degradation and instruction bloat.

The user should never have to ask the agent to run benchmarks or compare performance metrics after modifying code.

## Benchmark colocation under the one name rule

Each performance critical component maintains a dedicated benchmark target located inside `targets/<name>/<name>BENCH/`.

Just as unit tests reside in `<name>TEST/`, isolated component benchmarks live in `<name>BENCH/`. Agents must place benchmark workloads and their corresponding `baseline.json` files directly inside the owning component benchmark directory.

Benchmarks must link only their owning component target and `perf_scope` to ensure zero database setup or application clutter.

## Mandatory execution under optimized build presets

Benchmarks and hardware performance counter measurements must always run under an optimized build preset.

Agents must compile and execute benchmarks using presets such as `Clang_Custom_Release` or `GNU_Custom_Release`. Debug builds disable inlining and omit optimization flags, which distorts instruction counts and cache behavior.

Never evaluate benchmark scores or baseline regressions against unoptimized debug binaries.

## Automated baseline comparison and score evaluation

Every benchmark execution must evaluate current performance metrics against the birth baseline stored in `baseline.json`.

Agents must inspect the benchmark console output or the generated report in `production_artifacts/profiling/<name>_score_report.md`. The report tracks wall clock latency, CPU cycles, retired instructions, and instructions per cycle.

Any noticeable regression in latency or instruction counts must be resolved before presenting changes as complete.

## Bounded optimization and diminishing returns

Agents must respect human configured utility curves and target scores.

When the evaluation decision reaches `target_score_reached` or indicates `stop_diminishing_returns`, agents must stop further optimization loops. Avoid unbounded micro-optimizations that consume excessive context tokens without delivering meaningful speedups.

## Hardware performance metric verification

Agents must verify physical hardware PMU metrics alongside wall clock latency.

Inspect CPU cycles per operation, retired instructions per operation, branch misses, and cache misses. A sudden rise in retired instructions or branch mispredictions reveals unexpected abstraction penalties, hidden allocations, or missed compiler vectorization.
