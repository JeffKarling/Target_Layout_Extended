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
Plan component catalog entries following `.agents/rules/boundary_exceptions_and_catalog_logging.md`. Catalogs serve as the public interface documentation. Readers should understand high level component capabilities from catalog entries alone without reading the implementation. Rethrowing more than one exception across a component boundary is a code smell. A focused component maintains a single boundary catch all firewall paired with an error catalog entry, while defining generous public catalog log entries across all severity levels.

5. **Verification duo scope.**
Determine happy paths, boundary edge cases, and invalid inputs for `targets/<name>/<name>TEST/`. Determine the hot algorithmic workload and initial birth metric targets for `targets/<name>/<name>BENCH/`.

6. **Component isolation opportunities.**
Review `.agents/rules/component_segmentation_opportunities.md`. Determine whether the component benefits from isolated configuration advantages:
* Can the internal implementation use newer language standard features privately without impacting consumers?
* Does this component contain hot numerical routines that warrant local SIMD extensions or fast math flags?
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

## Step 3. Define catalog entries

Document operational events and boundary errors in `targets/<name>/<name>.catalog`.

Catalogs serve as the public interface documentation. By reading public catalog definitions, the reader should understand the component's high level features and capabilities without reading the actual code. The more catalog entries that are used to define distinct operations and checkpoints, the better the reader understands what the component does.

Rethrowing more than one exception across a boundary is a code smell. However, adding multiple public catalog log entries across different severity levels represents good design. Catalog logs are purely informative and do not steer application logic.

Follow `.agents/rules/boundary_exceptions_and_catalog_logging.md` to define informative events alongside the boundary failure entry:

```text
-- 51a3b901c2d44f6e87a213098df24101
@ COMPONENT=<name>
@ SUMMARY=Component initialization completed successfully

-- 51a3b901c2d44f6e87a213098df24102
@ COMPONENT=<name>
@ SUMMARY=Workload throughput fell below optimal threshold

-- 51a3b901c2d44f6e87a213098df24103
@ COMPONENT=<name>
@ SUMMARY=Unhandled fault caught at component boundary firewall
```

Declare corresponding constants in `targets/<name>/<name>/<name>.hpp`:

```cpp
namespace target_layout_extended::<name>::catalog {
    inline constexpr journal::catalog_entry k_initialized{
        .message_id = "51a3b901c2d44f6e87a213098df24101",
        .component = "<name>",
        .summary = "Component initialization completed successfully"
    };
    inline constexpr journal::catalog_entry k_throughput_warning{
        .message_id = "51a3b901c2d44f6e87a213098df24102",
        .component = "<name>",
        .summary = "Workload throughput fell below optimal threshold"
    };
    inline constexpr journal::catalog_entry k_boundary_fault{
        .message_id = "51a3b901c2d44f6e87a213098df24103",
        .component = "<name>",
        .summary = "Unhandled fault caught at component boundary firewall"
    };
} // namespace target_layout_extended::<name>::catalog
```

## Step 4. Add comprehensive unit tests

Scaffold `targets/<name>/<name>TEST/` before completing the implementation.
Add unit tests covering:
* Typical valid inputs and happy paths.
* Boundary conditions and edge cases.
* Invalid inputs caught safely without leaking exceptions.

## Step 5. Implement logic, logging, and boundary exception firewall

Implement the member functions in `targets/<name>/<name>.cpp`.
Emit informative catalog logs during normal execution. Enclose the public boundary in a single `try` block, delegating any unhandled fault to `journal::catch_boundary_exception()`:

```cpp
auto <name>::process(std::span<const std::byte> payload) noexcept -> bool {
    try {
        // Implementation logic
        journal::log_info(catalog::k_initialized, "Processing started");
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(catalog::k_boundary_fault);
    }
}
```

Never expose multiple exception pathways to callers. Use value based status returns for domain outcomes.

## Step 6. Verify under debug

Compile and execute unit tests:

```bash
cmake --build Build/Clang_Custom_Debug
ctest --test-dir Build/Clang_Custom_Debug --output-on-failure
```

Confirm that 100% of test targets pass cleanly.

## Step 7. Capture birth benchmark baseline

Configure and compile the component benchmark in `targets/<name>/<name>BENCH/`:

```bash
cmake --build Build/Clang_Custom_Release --target <name>BENCH
./Build/Clang_Custom_Release/targets/<name>/<name>BENCH/<name>BENCH
```

Verify that initial physical metrics are saved to `baseline.json`.
