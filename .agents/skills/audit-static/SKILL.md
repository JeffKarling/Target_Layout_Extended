---
name: audit-static
description: >-
  Runs compile-time static analysis, Clang-Tidy checks, Cppcheck scans and Include-What-You-Use header hygiene.
  Use this skill when the user asks to run static analysis, check code quality or audit header includes.
---

# Static analysis and header hygiene workflow

Use this runbook to audit code structure, detect bugs before runtime and maintain minimal header dependencies.

## Step 1. Run Clang-Tidy static analysis

Configure and build using the dedicated `Clang_Tidy` preset:

```bash
cmake --preset Clang_Tidy
cmake --build --preset Clang_Tidy
```

Triage reported warnings into three categories:
* **Category A critical issues.** Bugprone patterns, analyzer warnings and core guideline violations must be resolved immediately.
* **Category B design notes.** Situational checks like `readability-magic-numbers` should be reviewed and logged in `production_artifacts/clang_tidy_state.md`.
* **Category C stylistic noise.** Minor naming warnings can be suppressed locally with a targeted comment when intentional.

## Step 2. Run Cppcheck static scan

Enable Cppcheck inline analysis with C++23 rules:

```bash
cmake --preset GNU_Custom_Debug -DENABLE_CPPCHECK=ON
cmake --build --preset GNU_Custom_Debug
```

Review findings for null pointer dereferences, array index bounds risks, uninitialized variables and resource leaks.

## Step 3. Run Include-What-You-Use header hygiene

Audit header includes using the project mapping file `iwyu_mappings.imp`:

```bash
cmake --preset GNU_Custom_Debug -DENABLE_IWYU=ON
cmake --build --preset GNU_Custom_Debug
```

Apply Include-What-You-Use corrections:
* Ensure every component directly includes the standard and project headers it uses.
* Remove unused transitive headers from `<name>.hpp` and `<name>.cpp`.
* Keep machine generated headers out of public header files.

## Step 4. Verification

Run the dual compiler custom debug verification script with warnings treated as errors to confirm all static analysis passes and strict compiler warning gates complete cleanly:

```bash
cmake --preset GNU_Custom_Debug -DENABLE_WARNING_AS_ERROR=ON && cmake --build --preset GNU_Custom_Debug
cmake --preset Clang_Custom_Debug -DENABLE_WARNING_AS_ERROR=ON && cmake --build --preset Clang_Custom_Debug
```

Ensure no warnings or static analysis diagnostics remain in modified components, and reset `-DENABLE_IWYU=OFF -DENABLE_CPPCHECK=OFF` when finished.
