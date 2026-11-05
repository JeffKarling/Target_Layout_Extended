# Target Layout Extended

[![CI](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/ci.yml/badge.svg)](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/ci.yml)
[![Documentation](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/docs.yml/badge.svg)](https://github.com/JeffKarling/Target_Layout_Extended/actions/workflows/docs.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/JeffKarling/Target_Layout_Extended/badge)](https://securityscorecards.dev/viewer/?run_id=JeffKarling/Target_Layout_Extended)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/License-MIT--0-green.svg)](LICENSE)

*'The component is the CMake target, extended for production.'*

This repository builds directly on **[Target Layout](https://github.com/JeffKarling/Target_Layout)**. Where the base project proves the core rule in its minimal form, **Target Layout Extended** shows how a physical component scales in production Linux C++ engineering.

Every directory under `targets/` is an isolated physical component with an acyclic compilation firewall. Public contracts remain separated from private implementations, colocated tests, and production facilities.

## Production extensions

* **Boundary exception safety.** Private implementations catch internal failures using centralized Lippincott handlers without leaking exceptions across target boundaries.
* **Structured message catalogs.** Components record operational failures to `systemd-journald` through permanent UUID message catalogs.
* **Zero overhead binary diagnostics.** Boundary exceptions freeze pre unwind call frames as raw binary pointers in `systemd-journald` without string formatting or DWARF lookup overhead, ready for offline symbolization with `addr2line`.
* **Hardware counter benchmarking.** Components benchmark runtime performance against hardware counter baselines and utility curves.
* **Centralized target configuration.** Components share compiler options and analysis settings without custom wrapper macros.
* **Code generation as a sibling component.** Schemas such as Protobuf live in dedicated targets that expose generated headers transparently.
* **Multi compiler toolchains.** Presets support GNU GCC and LLVM Clang with sanitizers, static analysis, and standard library hardening.

## Quickstart

Build the project across GNU GCC and LLVM Clang using the build scripts.

```bash
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
```

Run an isolated hardware counter benchmark and evaluate its score curve.

```bash
cmake --build --preset GNU_Custom_Release --target tbb_algosBENCH_run
```

## Detailed documentation

For deep architectural rationale and implementation walkthroughs, refer to the documentation suite.

* [How it works](Documentation/index.md). Core principles, component anatomy, and centralized target configuration.
* [Component boundaries and catalogs](Documentation/component_boundaries.md). Boundary exception handling, Lippincott routines, and message catalogs.
* [Adding a component](Documentation/adding_a_component.md). Scaffolding runbook, CMake templates, and catalog contracts.
* [Benchmarking and score optimization](Documentation/benchmarking_and_profiling.md). PMU hardware counters, utility curves, and regression checks.
* [Presets and multi compiler workflow](Documentation/presets_and_compilers.md). Dual compiler matrix, build presets, and standard library hardening.
* [Static analysis and deep debug](Documentation/static_analysis_and_debug.md). Clang-Tidy, Cppcheck, and Include-What-You-Use checks.
* [Runtime sanitizers](Documentation/sanitizer_guide.md). AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer configurations.

## License

Licensed under [MIT-0](LICENSE).
