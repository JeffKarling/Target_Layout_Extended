---
name: audit-sans
description: >-
  Runs unit test suites across dual compilers Clang and GCC, standard library deep hardening, and runtime sanitizers ASan, UBSan, and TSan.
  Use this skill when the user asks to run sanitizers, check for memory leaks, data races, undefined behavior, or cross-compiler parity.
---

# Runtime sanitizers and dual compiler audit workflow

Use this runbook to catch memory corruption, data races, undefined behavior, and toolchain incompatibilities.

## Step 1. AddressSanitizer and leak detection

Run all component unit tests under AddressSanitizer across both Clang and GCC workflow presets, plus standalone LeakSanitizer:

```bash
cmake --workflow --preset Clang_ASan_Verify
cmake --workflow --preset GNU_ASan_Verify
cmake --workflow --preset Clang_LSan_Verify
```

AddressSanitizer and LeakSanitizer detect heap buffer overflows, stack out of bounds access, use after free defects, and process memory leaks.
If a sanitizer report is emitted, inspect the printed stack trace to identify the offending source line.

## Step 2. UndefinedBehaviorSanitizer pass

Run all component unit tests under UndefinedBehaviorSanitizer across both compilers:

```bash
cmake --workflow --preset Clang_UBSan_Verify
cmake --workflow --preset GNU_UBSan_Verify
```

This pass detects signed integer overflows, null pointer dereferences, misaligned pointer accesses, and invalid enum representations. Note that `Clang_UBSan` passes `-fno-sanitize-recover=undefined` to abort immediately on undefined behavior.

## Step 3. ThreadSanitizer multithreaded pass

Run multithreaded components under ThreadSanitizer:

```bash
cmake --workflow --preset Clang_TSan_Verify
```

ThreadSanitizer inspects concurrent memory accesses in `journal`, `tbb_algos`, and `database_manager` to flag unprotected shared state reads, atomic memory order defects, and lock order inversions.

## Step 4. Dual toolchain parity check

Compile and test the repository across both GCC and Clang in custom debug and portable default release modes:

```bash
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
```

Confirm that code compiles without warnings under both compilers and that no vendor specific extensions were introduced.

## Step 5. Deep debug standard library hardening

Execute deep debug builds across both compilers (`_GLIBCXX_DEBUG_PEDANTIC` on GCC and `_LIBCPP_HARDENING_MODE_EXTENSIVE` plus `_GLIBCXX_ASSERTIONS` on Clang) to validate standard library preconditions and iterator ranges:

```bash
./production_artifacts/build_all.sh custom deep
```

This verifies that container indexing and iterator operations remain strictly within valid bounds across both toolchains.
