# Runtime sanitizers

**Target Layout Extended** provides seven runtime sanitizer presets across Clang and GCC. Sanitizers instrument every memory access, integer operation, and thread synchronization primitive at compile time so the compiled unit tests (`<name>TEST`) and `app` executable catch memory corruption, data races, uninitialized reads, and undefined behavior at runtime with exact source line stack traces.

## 1. The three layer safety net

```text
┌─────────────────────────────────────────────────────────────┐
│  Layer 1. Compile Time Warnings and Static Analysis         │
│  • -Wall -Wextra -Wpedantic -Wconversion + Clang-Tidy       │
│  • Zero runtime overhead                                    │
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

## 2. Sanitizer preset summary

| Preset | Compiler | Bug family detected | Workflow preset |
| --- | --- | --- | --- |
| `Clang_ASan` | `clang++` | Heap, stack, and global buffer overflows, use after free | `Clang_ASan_Verify` |
| `Clang_UBSan` | `clang++` | Signed integer overflow, null dereference, misaligned access | `Clang_UBSan_Verify` |
| `Clang_TSan` | `clang++` | Multithreaded data races and lock order inversions | `Clang_TSan_Verify` |
| `Clang_LSan` | `clang++` | Standalone heap memory leak detection at process exit | `Clang_LSan_Verify` |
| `Clang_MSan` | `clang++` | Reads from uninitialized stack or heap memory | `Clang_MSan` |
| `GNU_ASan` | `g++` | Heap, stack, and global buffer overflows, use after free | `GNU_ASan_Verify` |
| `GNU_UBSan` | `g++` | Undefined behavior across arithmetic and pointers | `GNU_UBSan_Verify` |

> [!IMPORTANT]
> Sanitizers are mutually exclusive because they use incompatible shadow memory layouts. Never combine `ASan`, `TSan`, and `MSan` in the same binary. Instead, run each sanitizer workflow preset sequentially.

## 3. Running unit tests under sanitizers

Unlike templates that disable unit tests during sanitizer builds, **Target Layout Extended** keeps `BUILD_TESTING` enabled so every component's `<name>TEST` suite is compiled and executed under the active sanitizer via `ctest`.

Run a full configure, build, and test verification pass in one command using CMake workflow presets.

```bash
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

```bash
cmake --preset Clang_ASan && cmake --build --preset Clang_ASan
./Build/Clang_ASan/targets/database_manager/database_managerTEST/database_managerTEST
```

## 4. Key sanitizer implementation details

In `cmake/templates/target_compile_options.cmake`, sanitizer flags are applied to both `target_compile_options` and `target_link_options`. Passing `-fsanitize=*` only at compile time or only at link time produces an uninstrumented or broken binary.

* **Blocking UBSan in Clang (`-fno-sanitize-recover=undefined`).** By default, UBSan prints a warning and continues running, which allows `ctest` to report a passing exit code even when undefined behavior occurred. Adding `-fno-sanitize-recover=undefined` causes the test binary to abort immediately with a stack trace so `ctest` fails loudly.
* **Position independent executables for TSan and LSan (`-fPIE` and `-pie`).** ThreadSanitizer and LeakSanitizer reserve specific virtual address shadow regions on Linux and require `-fPIE` at compile time and `-pie` at link time.
* **Frame pointer preservation (`-fno-omit-frame-pointer` and `-fno-optimize-sibling-calls`).** Every sanitizer preset disables frame pointer omission and tail call elimination at `-O2` so fast shadow memory stack unwinders always produce complete, accurate call stacks.
* **MemorySanitizer note (`Clang_MSan`).** MSan tracks uninitialized bit propagation across all instructions (`-fsanitize-memory-track-origins=2`). Because MSan requires every linked C++ library including `libstdc++` or `libc++` and `libprotobuf` to be compiled with `-fsanitize=memory` to avoid false positives inside uninstrumented system libraries, use `Clang_MSan` on self contained components that do not link prebuilt system shared libraries.
