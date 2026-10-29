# Static analysis and deep debug

Before running binary sanitizers, **Target Layout Extended** enforces two earlier verification layers. Layer 1 checks code structure at compile time using `clang-tidy`, `include-what-you-use`, and `cppcheck`. Layer 2 validates standard library container invariants and iterator preconditions at runtime using deep debug presets.

## 1. Static analysis with `Clang_Tidy`

Running `clang-tidy` across all hand written components requires two commands using the `Clang_Tidy` preset.

```bash
cmake --preset Clang_Tidy
cmake --build --preset Clang_Tidy
```

In `cmake/templates/target_properties.cmake`, `CXX_CLANG_TIDY` is attached automatically when `CMAKE_BUILD_TYPE` is `Clang_Tidy`. Machine generated components such as `targets/item_schema/` clear `CXX_CLANG_TIDY` on their own target so generated Protobuf files never pollute static analysis output.

### Warning classification and `clang_tidy_state.md`

To prevent warning fatigue while keeping active builds clean, findings are grouped into three categories.

* **Category A safety critical checks.** Bugprone patterns, analyzer defects, and core guideline violations that must be fixed immediately before committing.
* **Category B situational design warnings.** Checks such as `readability-magic-numbers` that warrant periodic review and are logged in `production_artifacts/clang_tidy_state.md`.
* **Category C stylistic noise.** Minor identifier length or formatting warnings logged in `production_artifacts/clang_tidy_state.md` or suppressed locally with a targeted comment when intentional.

## 2. Dependency hygiene with `ENABLE_IWYU` and `ENABLE_CPPCHECK`

Two additional static analyzers can be toggled on any configure preset via CMake options.

```bash
# Run Include-What-You-Use header hygiene analysis
cmake --preset GNU_Custom_Debug -DENABLE_IWYU=ON
cmake --build --preset GNU_Custom_Debug

# Run Cppcheck static analysis inline during compilation
cmake --preset GNU_Custom_Debug -DENABLE_CPPCHECK=ON
cmake --build --preset GNU_Custom_Debug
```

* **Include What You Use (`iwyu_mappings.imp`).** Ensures every component `.hpp` and `.cpp` file directly includes the headers it uses and removes unused transitive includes.
* **Cppcheck (`ENABLE_CPPCHECK`).** Runs inline with C++23 enabled to surface null pointer dereferences, bounds errors, and resource leaks.

## 3. Deep debug standard library hardening

Standard debug builds (`-O0 -g`) map machine instructions back to source lines, but they do not validate standard library container operations unless hardening macros are enabled. Out of bounds `std::vector::operator[]` indexing or dereferencing an invalidated iterator can silently corrupt memory without crashing.

**Target Layout Extended** provides two levels of standard library checks in `cmake/templates/target_compile_options.cmake`.

### Fast debug assertions (`GNU_Custom_Debug` and `Clang_Custom_Debug`)

Enabled automatically in daily custom debug builds with minimal overhead and full ABI compatibility.

* **GCC and Clang with `libstdc++`.** Defines `_GLIBCXX_ASSERTIONS` to enable bounds checks on `std::vector`, `std::string`, `std::span`, and `std::optional` dereferences.
* **Clang with `libc++`.** Defines `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG`.

### Exhaustive deep debug (`GNU_Custom_Debug_Deep` and `Clang_Custom_Debug_Deep`)

Run across both compilers using the unified runner.

```bash
./production_artifacts/build_all.sh custom deep
```

* **GNU Deep Debug (`_GLIBCXX_DEBUG` and `_GLIBCXX_DEBUG_PEDANTIC`).** Replaces standard containers with safe debug wrappers that track every iterator's attachment to its parent container. Dereferencing an iterator after a container reallocation or comparing iterators from two different containers aborts immediately with an exact diagnostic message.
* **Clang Deep Debug (`_LIBCPP_HARDENING_MODE_EXTENSIVE` and `_GLIBCXX_ASSERTIONS`).** Activates maximum non ABI breaking precondition and bounds assertions across standard library operations.

| Diagnostic capability | `GNU_Custom_Debug_Deep` | `Clang_Custom_Debug_Deep` |
| --- | --- | --- |
| Bounds checking on indexing | Yes | Yes |
| Iterator invalidation tracking | Full container wrapper tracking | Precondition assertions |
| ABI compatibility with external binaries | Changes container layout | Preserves container layout |

 Note that if a component passes `std::vector` or `std::string` across a precompiled system shared library boundary such as `libprotobuf.so`, `_GLIBCXX_DEBUG` changes the binary layout of `std::vector`. Keep public third party serialization calls isolated inside dedicated components or test with `GNU_Custom_Debug` (`_GLIBCXX_ASSERTIONS`) when linking prebuilt system C++ libraries.
