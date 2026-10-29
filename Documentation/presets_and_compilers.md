# Presets and multi compiler workflow

C++ projects face a tension between local developer velocity and downstream consumer portability. Developers need host CPU optimizations via `-march=native`, DWARF-5 debug symbols, frame pointers, and strict compiler warnings. Package maintainers and CI environments need neutral builds that compile on any x86-64 machine without hardcoded instruction set overrides.

**Target Layout Extended** solves this through a **dual preset strategy** in `CMakePresets.json` across both **GNU GCC** and **LLVM Clang**.

## Custom versus default presets

Every compiler family defines two categories of presets.

```text
                  ┌───────────────────────────────┐
                  │      Developer Code Change    │
                  └───────────────┬───────────────┘
                                  │
                  ┌───────────────┴───────────────┐
                  │   Double Verification Cycle   │
                  └──────┬─────────────────┬──────┘
                         │                 │
         ┌───────────────┴───────┐ ┌───────┴───────────────┐
         │ Custom Presets        │ │ Default Presets       │
         │ Active Development    │ │ Portable Distribution │
         ├───────────────────────┤ ├───────────────────────┤
         │ • -march=native       │ │ • Standard CMake flags│
         │ • Maximum warnings    │ │ • Generic target arch │
         │ • STL hardening       │ │ • Portable binaries   │
         └───────────────┬───────┘ └───────┬───────────────┘
                         │                 │
                         └────────┬────────┘
                                  │
                  ┌───────────────┴───────────────┐
                  │     Verified Across Both      │
                  └───────────────────────────────┘
```

### Custom development presets

Named with `Custom` such as `GNU_Custom_Debug`, `Clang_Custom_Debug`, `GNU_Custom_Release`, and `Clang_Custom_Release`.

* **Host CPU instruction sets.** Passes `-march=native -O3 -ffast-math -fstack-protector-strong` in release builds to unlock AVX2, AVX-512, and FMA vectorization on the developer's machine.
* **Accurate stack traces.** Debug and profiling builds enable `-gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls` so Linux `perf` call graphs and sanitizer stack traces retain call frames.
* **Strict warning profiles.** Enables `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2` plus compiler specific diagnostics.

### Default portable presets

Named with `Default` such as `GNU_Default_Debug`, `Clang_Default_Debug`, `GNU_Default_Release`, and `Clang_Default_Release`.

* **Standard CMake flags.** Relies on CMake's built in `Debug`, `Release`, and `RelWithDebInfo` build types without injecting `-march=native` or custom warning flags.
* **Distribution readiness.** Confirms that external users and CI pipelines can build the repository on any standard Linux machine.

## Why verify across both GNU GCC and LLVM Clang

Compiling and testing every component under both `g++` and `clang++` catches defects that a single compiler misses.

* **Distinct front end diagnostics.** Clang flags initialization order, bitfield padding, and exit time destructor issues that GCC allows, while GCC catches string operation overflow and logical condition duplication patterns that Clang ignores.
* **Independent optimizer passes.** Code that accidentally relies on undefined behavior often passes under one optimizer backend and fails unit tests under the other.
* **Complementary sanitizer and STL hardening support.** GCC provides full structural iterator checking via `_GLIBCXX_DEBUG_PEDANTIC`, while Clang provides standalone `MemorySanitizer`, `ThreadSanitizer`, and `LeakSanitizer` presets.

## CMake workflow presets and build_all.sh

CMake 3.25+ **workflow presets** chain configuration, compilation, and `ctest` execution into a single command for a given toolchain.

```bash
cmake --workflow --preset GNU_Custom_Debug_Verify
cmake --workflow --preset Clang_Custom_Debug_Verify
cmake --workflow --preset GNU_Custom_Release_Verify
cmake --workflow --preset Clang_Custom_Release_Verify
```

Because CMake restricts each workflow preset to a single compiler toolchain, `production_artifacts/build_all.sh` runs both GNU and Clang verification workflows sequentially in one command.

```bash
# Verify custom development presets in debug mode across GNU and Clang
./production_artifacts/build_all.sh custom debug

# Verify portable default presets in release mode across GNU and Clang
./production_artifacts/build_all.sh default release

# Verify deep debug STL hardening across GNU and Clang
./production_artifacts/build_all.sh custom deep
```
