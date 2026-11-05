---
name: feature
description: >-
  Scaffolds and implements new physical components or functional capabilities under Target Layout Extended.
  Use this skill when the user asks to add, create, or scaffold a new component, library target, or feature.
---

# Feature development workflow

Use this runbook to design, scaffold, implement, and verify a new component.

## Step 0. Architectural decision tree

Before drafting code or scaffolding files, walk through the six foundational decisions required by Target Layout Extended:

1. **Component granularity.**
Determine whether the capability belongs inside an existing component or warrants a new component under the physical one name rule. If the responsibility spans both runtime processing and serialization or storage, decide whether to split it into two distinct components.

2. **Dependency graph positioning.**
Map where the component sits in the acyclic dependency graph. Identify which components it links. Ensure all dependencies link `PRIVATE` unless public headers directly expose types from those dependencies. Check whether an existing dependency suffices or if a new third party library is required.

3. **Public boundary contract.**
Design the interface in `targets/<name>/<name>/<name>.hpp`. Follow Scott Meyers rules from `.agents/rules/meyers_interface_design.md` and ranges guidelines from `.agents/rules/ranges_and_views.md`. Determine if the component is an operational boundary requiring `noexcept` status returns.

4. **Catalog error taxonomy and logging.**
Plan component catalog entries following `.agents/rules/boundary_exceptions_and_catalog_logging.md`. Catalogs in `<name>.catalog` and `struct catalog` document the public boundary failure contracts of the component. Operational components must never leak exceptions across their public boundary. A focused operational component catches unhandled exceptions at its boundary firewall and transforms them into boolean status via `journal::catch_boundary_exception`. It emits internal operational diagnostics via `journal::log` across all eight Syslog severities into the lock free flight recorder ring buffer.

5. **Verification duo scope.**
Determine happy paths, boundary edge cases, and invalid inputs for `targets/<name>/<name>TEST/`. Determine the hot algorithmic workload and initial birth metric targets for `targets/<name>/<name>BENCH/`.

6. **Component isolation opportunities.**
Review `.agents/rules/component_segmentation_opportunities.md` and `.agents/rules/override_shared_target_defaults.md`. Determine whether the component benefits from isolated configuration advantages:
* Should helper logic stay in a private `<helper>.cpp` at the component root or be split into a new sibling component under `targets/`?
* Can the internal implementation use newer language standard features privately without impacting consumers?
* Does this component contain hot numerical routines that warrant local SIMD extensions or require `-fno-fast-math` for strict IEEE 754 floating point semantics?
* Does this component wrap generated code or external dependencies requiring isolated warning filters?

## Step 1. Draft the public interface

Design the component header in `targets/<name>/<name>/<name>.hpp`.

Follow Scott Meyers principles from `.agents/rules/meyers_interface_design.md`:
* Wrap primitive types in strong types or scoped enums.
* Make invalid states unrepresentable.
* Use static factory functions returning `std::optional` or `std::expected` for fallible initialization.
* Accept range concepts like `template <std::ranges::input_range R>` or `std::span` rather than concrete containers.

## Step 2. Scaffold the physical component

Create the component directory structure under `targets/<name>/`:

```text
targets/<name>/
├── CMakeLists.txt
├── <name>.cpp
├── <name>.catalog
├── <name>/
│   └── <name>.hpp
├── <name>TEST/
│   ├── CMakeLists.txt
│   └── <name>TEST.cpp
└── <name>BENCH/
    ├── CMakeLists.txt
    ├── <name>BENCH.cpp
    └── baseline.json
```

In `targets/<name>/CMakeLists.txt`, derive target properties using `${DIR_NAME}`.
Both `target_properties.cmake` and `target_compile_options.cmake` must always be included. If a component requires custom flags or warning suppressions, follow `.agents/rules/override_shared_target_defaults.md` to override defaults locally.
Link internal dependencies privately:

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_sources(${DIR_NAME}
        PRIVATE
        ${DIR_NAME}.cpp

        PUBLIC
        FILE_SET ${DIR_NAME}
        TYPE HEADERS
        FILES
        ${DIR_NAME}/${DIR_NAME}.hpp
)

target_link_libraries(${DIR_NAME}
        PUBLIC
        ${PROJECT_NAME}::journal
)

if(BUILD_TESTING)
    add_subdirectory(${DIR_NAME}TEST)
endif()
```

## Step 3. Define boundary catalog entries

Document public boundary failure contracts in `targets/<name>/<name>.catalog`.

Catalog entries are reserved strictly for `noexcept` boundary failures caught by `journal::catch_boundary_exception`. Small components typically define a single boundary failure entry, which the compiler deduces automatically. Larger components can define multiple entries in `struct catalog` and select them explicitly via `journal::catalog_exception` and `journal::catch_boundary_exception`.

Declare the closed `struct catalog` in `targets/<name>/<name>/<name>.hpp`:

```cpp
namespace tle::<name> {
    struct catalog {
        journal::catalog_entry k_boundary_fault{
            .message_id = "51a3b901c2d44f6e87a213098df24103",
            .component = "<name>",
            .summary = "Unhandled fault caught at component boundary firewall"
        };
    };
    inline constexpr catalog k_catalog{};
} // namespace tle::<name>
```

## Step 4. Add comprehensive unit tests

Scaffold `targets/<name>/<name>TEST/` before completing the implementation.
Add unit tests covering:
* Compile time `static_assert(tle::journal::is_valid_catalog(k_catalog));`.
* Typical valid inputs and happy paths.
* Boundary conditions and edge cases.
* Invalid inputs caught safely without leaking exceptions.
* Diagnostic log verification in memory when needed using `journal::detail::set_record_sink`, `journal::flush`, and `journal::detail::reset_record_sink`.

## Step 5. Implement logic, logging, and boundary exception firewall

Implement the member functions in `targets/<name>/<name>.cpp` using `<ranges>` and `std::views` without naked C style `for` loops.
Emit internal diagnostics using `journal::log` with strong types `journal::severity`, `journal::message`, and `journal::binary_payload`. Enclose the public boundary in a single `try` block, delegating any unhandled fault to `journal::catch_boundary_exception`:

```cpp
auto <name>::process(std::span<const std::byte> payload) noexcept -> bool {
    try {
        // Implementation logic
        journal::log(journal::severity::info, journal::message{"processing started"});
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(k_catalog);
    }
}
```

Never expose multiple exception pathways to callers. Use value based status returns for domain outcomes.

## Step 6. Verify across both compilers

Compile and execute unit tests across both GNU GCC and LLVM Clang:

```bash
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
```

Confirm that 100% of test targets pass cleanly under both toolchains.

## Step 7. Capture birth benchmark baseline

Configure and execute the component benchmark in `targets/<name>/<name>BENCH/` under an optimized custom release preset:

```bash
cmake --build --preset Clang_Custom_Release --target <name>BENCH_run
```

Verify that initial physical metrics are saved to `baseline.json`.
