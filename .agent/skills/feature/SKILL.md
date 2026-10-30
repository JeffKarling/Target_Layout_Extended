---
name: feature
description: >-
  Scaffolds and implements new physical components or functional capabilities under Target Layout Extended.
  Use this skill when the user asks to add, create, or scaffold a new component, library target, or feature.
---

# Feature development workflow

Use this runbook to design, scaffold, implement, and verify a new component.

## Step 1. Draft the public interface

Design the component header in `targets/<name>/<name>/<name>.hpp`.

Follow Scott Meyers principles from `.agent/rules/meyers_interface_design.md`:
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

Document boundary exceptions in `targets/<name>/<name>.catalog`:

```text
-- 8e4c9201b7a34d568f120c93e5d7b301
@ COMPONENT=<name>
@ SUMMARY=Detailed failure summary caught at boundary
```

## Step 4. Add comprehensive unit tests

Scaffold `targets/<name>/<name>TEST/` before completing the implementation.
Add unit tests covering:
* Typical valid inputs and happy paths.
* Boundary conditions and edge cases.
* Invalid inputs caught safely without leaking exceptions.

## Step 5. Implement logic and catch boundary exceptions

Implement the member functions in `targets/<name>/<name>.cpp`.
Enclose boundary entry points in `try` blocks and delegate failures to `journal::catch_boundary_exception()`.

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
