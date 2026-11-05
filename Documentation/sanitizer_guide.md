# Runtime sanitizers

**Target Layout Extended** provides runtime sanitizer presets across Clang and GCC. Sanitizers instrument memory accesses, integer operations and thread synchronization at compile time. Compiled unit tests in `<name>TEST` and the `app` executable catch memory corruption, data races, uninitialized reads and undefined behavior at runtime with source line stack traces.

## The three layer safety net

```text title="Verification layers from compile time to runtime"
┌─────────────────────────────────────────────────────────────┐
│  Layer 1. Compile Time Warnings and Static Analysis         │
│  • -Wall -Wextra -Wpedantic -Wconversion + Clang-Tidy       │
│  • Compile time checks only                                 │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│  Layer 2. Standard Library Assertions                       │
│  • _GLIBCXX_ASSERTIONS / _LIBCPP_HARDENING_MODE             │
│  • Validates STL container preconditions and bounds         │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│  Layer 3. Runtime Sanitizers                                │
│  • Instruments machine instructions and shadow memory       │
│  • Detects heap corruption, data races, leaks, and UB       │
└─────────────────────────────────────────────────────────────┘
```

## Sanitizer preset summary

| Preset | Compiler | Bug family detected | Workflow preset |
| --- | --- | --- | --- |
| `Clang_ASan` | `clang++` | Heap, stack, and global buffer overflows, use after free | `Clang_ASan_Verify` |
| `Clang_UBSan` | `clang++` | Signed integer overflow, null dereference, misaligned access | `Clang_UBSan_Verify` |
| `Clang_TSan` | `clang++` | Multithreaded data races and lock order inversions | `Clang_TSan_Verify` |
| `Clang_LSan` | `clang++` | Standalone heap memory leak detection at process exit | `Clang_LSan_Verify` |
| `Clang_MSan` | `clang++` | Reads from uninitialized stack or heap memory | None, configure and build only |
| `GNU_ASan` | `g++` | Heap, stack, and global buffer overflows, use after free | `GNU_ASan_Verify` |
| `GNU_UBSan` | `g++` | Undefined behavior across arithmetic and pointers | `GNU_UBSan_Verify` |

!!! important "Sanitizers are mutually exclusive"
    Sanitizers use incompatible shadow memory layouts. Do not combine `ASan`, `TSan`, and `MSan` in the same binary. Instead, run each sanitizer workflow preset sequentially.

## Running unit tests under sanitizers

Unlike templates that disable unit tests during sanitizer builds, **Target Layout Extended** keeps `BUILD_TESTING` enabled so every component's `<name>TEST` suite is compiled and executed under the active sanitizer via `ctest`.

Run a configure, build, and test verification pass in one command using CMake workflow presets.

```bash title="Execute sanitizer verification workflows"
# Run all component unit tests under AddressSanitizer
cmake --workflow --preset Clang_ASan_Verify
cmake --workflow --preset GNU_ASan_Verify

# Run all component unit tests under UndefinedBehaviorSanitizer
cmake --workflow --preset Clang_UBSan_Verify
cmake --workflow --preset GNU_UBSan_Verify

# Run all component unit tests under ThreadSanitizer
cmake --workflow --preset Clang_TSan_Verify

# Run all component unit tests under LeakSanitizer
cmake --workflow --preset Clang_LSan_Verify
```

Or run a single component's test binary directly after building.

```bash title="Run an isolated component test under AddressSanitizer"
cmake --preset Clang_ASan && cmake --build --preset Clang_ASan
./Build/Clang_ASan/targets/database_manager/database_managerTEST/database_managerTEST
```

## Key sanitizer implementation details

In `cmake/templates/target_compile_options.cmake`, sanitizer flags are applied to both `target_compile_options` and `target_link_options`. Passing `-fsanitize=*` only at compile time or only at link time produces an uninstrumented or broken binary.

* **Blocking UBSan in Clang only.** By default UBSan prints a warning and continues running, which lets `ctest` report success even when undefined behavior occurred. `Clang_UBSan` adds `-fno-sanitize-recover=undefined` so the test aborts and fails. `GNU_UBSan` does not pass that flag yet, so read its test output rather than trusting the exit code alone.
* **Assertions are disabled.** Every sanitizer configuration defines `NDEBUG`, so `assert()` checks do not run under sanitizers. Use the `*_Custom_Debug` presets to exercise assertions.
* **Position independent executables for TSan and LSan.** ThreadSanitizer and LeakSanitizer reserve specific virtual address shadow regions on Linux and require `-fPIE` at compile time and `-pie` at link time.
* **Frame pointer preservation.** Every sanitizer preset passes `-fno-omit-frame-pointer` and `-fno-optimize-sibling-calls` at `-O2` so shadow memory stack unwinders produce complete call stacks.
* **MemorySanitizer considerations.** `Clang_MSan` tracks uninitialized bit propagation via `-fsanitize-memory-track-origins=2`. MSan reports false positives unless every linked library is also instrumented, and that includes the C++ standard library and `libprotobuf`. This project links prebuilt system libraries, so treat `Clang_MSan` as a build check and run individual tests manually on self contained components.
