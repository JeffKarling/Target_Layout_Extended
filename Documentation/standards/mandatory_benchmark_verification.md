# Mandatory benchmark verification on code changes

## Core requirement

Every algorithmic modification, hot path refactoring or interface redesign must include benchmark execution. Engineers must actively prevent latency degradation and instruction bloat.

Engineers must run benchmarks and compare performance metrics proactively after modifying code.

## Benchmark colocation under the one name rule

Each performance critical component maintains a dedicated benchmark component located inside `targets/<name>/<name>BENCH/`.

Just as unit tests reside in `<name>TEST/`, isolated component benchmarks live in `<name>BENCH/`. Engineers must place benchmark workloads and their corresponding `baseline.json` files directly inside the owning component benchmark directory.

Benchmarks must link only their owning component and `perf_scope` to ensure zero database setup or application clutter.

## Mandatory execution under optimized build presets

Benchmarks and hardware performance counter measurements must always run under an optimized build preset.

Engineers must compile and execute benchmarks using presets such as `Clang_Custom_Release` or `GNU_Custom_Release`. Debug builds disable inlining and omit optimization flags, which distorts instruction counts and cache behavior.

Never evaluate benchmark scores or baseline regressions against unoptimized debug binaries.

## Host workstation instrumentation and performance mode

Engineers must ensure accurate hardware PMU instrumentation and stable clock frequencies when gathering baselines.

Linux kernels often set `/proc/sys/kernel/perf_event_paranoid` to 2 or higher by default. This setting restricts unprivileged access to hardware performance counters. Engineers should lower this threshold to 1 via `sysctl` on local development workstations.

```bash
sudo sysctl kernel.perf_event_paranoid=1
```

Dynamic CPU frequency governors introduce measurement noise across benchmark runs. Engineers should set the host system into performance mode before executing benchmarks. On Linux workstations running systemd and power-profiles-daemon, switch profiles using `powerprofilesctl`.

```bash
powerprofilesctl set performance
```

Alternatively, select the performance profile in the desktop system settings menu.

## Automated baseline comparison and score evaluation

Every benchmark execution must evaluate current performance metrics against the birth baseline stored in `baseline.json`.

Engineers must inspect the benchmark console output or the generated report in `production_artifacts/profiling/<name>_score_report.md`. The report tracks wall clock latency, CPU cycles, retired instructions and instructions per cycle.

Any noticeable regression in latency or instruction counts must be resolved before presenting changes as complete.

## Bounded optimization and diminishing returns

Engineers must respect human configured utility curves and target scores.

When the evaluation decision reaches `target_score_reached` or indicates `stop_diminishing_returns`, engineers must stop further optimization loops. Avoid unbounded micro-optimizations that deliver negligible speedups.

## Hardware performance metric verification

Engineers must verify physical hardware PMU metrics alongside wall clock latency.

Inspect CPU cycles, retired instructions, branch misses and cache misses. A sudden rise in retired instructions or branch mispredictions reveals unexpected abstraction penalties or missed compiler vectorization.

## Portable multi versioning consideration

When hardware counters indicate high instructions per cycle on a coarse computational loop, engineers must consider function multi versioning. Review [function multi versioning and target clones](function_multi_versioning_and_target_clones.md) for qualification criteria.

Annotating candidate loops with `target_clones` allows distribution packages compiled under default presets to deliver AVX2 and AVX-512 speedups automatically. Do not annotate small inlined helpers.
