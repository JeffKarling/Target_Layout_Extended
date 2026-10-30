---
name: audit-sans
description: >-
  Runs unit test suites across dual compilers Clang and GCC, standard library deep hardening, and runtime sanitizers ASan, UBSan, and TSan.
  Use this skill when the user asks to run sanitizers, check for memory leaks, data races, undefined behavior, or cross-compiler parity.
---

# Runtime sanitizers and dual compiler audit workflow

Use this runbook to catch memory corruption, data races, undefined behavior, and toolchain incompatibilities.

## Step 1. AddressSanitizer and leak detection

Run all component unit tests under AddressSanitizer using the CMake workflow preset:

```bash
cmake --workflow --preset Clang_ASan_Verify
```

AddressSanitizer detects heap buffer overflows, stack out of bounds access, use-after-free defects, and process memory leaks.
If a sanitizer report is emitted, inspect the printed stack trace to identify the offending source line.

## Step 2. UndefinedBehaviorSanitizer pass

Run all component unit tests under UndefinedBehaviorSanitizer:

```bash
cmake --workflow --preset Clang_UBSan_Verify
```

This pass detects signed integer overflows, null pointer dereferences, misaligned pointer accesses, and invalid enum representations.

## Step 3. ThreadSanitizer multithreaded pass

Run multithreaded components under ThreadSanitizer:

```bash
cmake --workflow --preset Clang_TSan_Verify
```

ThreadSanitizer inspects memory accesses in `tbb_algos` and `database_manager` to flag unprotected shared state reads and lock order inversions.

## Step 4. Dual toolchain parity check under GCC

Compile and test the repository under GCC to ensure cross-compiler portability:

```bash
cmake --preset GNU_Custom_Debug
cmake --build --preset GNU_Custom_Debug
ctest --test-dir Build/GNU_Custom_Debug --output-on-failure
```

Confirm that code compiles without warnings under GCC and that no Clang-specific extensions were introduced.

## Step 5. Deep debug standard library hardening

Execute deep debug builds to validate standard library preconditions and iterator ranges:

```bash
cmake --preset Clang_Custom_Debug_Deep
cmake --build --preset Clang_Custom_Debug_Deep
ctest --test-dir Build/Clang_Custom_Debug_Deep --output-on-failure
```

This verifies that container indexing and iterator operations remain strictly within valid bounds.
