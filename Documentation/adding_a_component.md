# Adding a component

Adding a new physical component requires no changes to the root `CMakeLists.txt`. The `targets/CMakeLists.txt` script automatically registers every subdirectory under `targets/` as a static CMake target at configure time.

This guide walks through creating a complete production component with boundary contracts, systemd catalogs and colocated verification suites.

## Directory scaffolding

Create the component directory under `targets/<name>/`.

```text title="Component directory layout"
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

Every filename mirrors the directory name derived through `${DIR_NAME}`. The `<name>BENCH/` directory is optional and reserved for components with performance sensitive algorithmic loops.

## Component build script

Place the target definition in `targets/<name>/CMakeLists.txt`.

```cmake title="targets/<name>/CMakeLists.txt"
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

Every operational component links `${PROJECT_NAME}::journal` as `PUBLIC` because its public header declares catalog entries. Any internal library dependencies must be linked `PRIVATE` unless public function signatures expose their types.

## Public contract header

Declare the public interface in `targets/<name>/<name>/<name>.hpp`.

```cpp title="targets/<name>/<name>/<name>.hpp"
#pragma once

#include <span>
#include <journal/journal.hpp>

namespace tle::<name> {

    struct catalog {
        journal::catalog_entry k_execution_failed{
            .message_id = "a1b2c3d4e5f60718293a4b5c6d7e8f90",
            .component = "<name>",
            .summary = "Workload execution failure caught at component boundary"
        };
    };
    inline constexpr catalog k_catalog{};

    [[nodiscard]] auto process_data(std::span<int> values) noexcept -> bool;

} // namespace tle::<name>
```

Public functions at subsystem boundaries are marked `noexcept` and return status values such as `bool` or `std::optional`. Generate the 32 character lowercase hexadecimal UUID using `systemd-id128 new` or `journalctl --new-id128`.

## Implementation and exception handling

Implement component logic in `targets/<name>/<name>.cpp`. Internal helpers throw standard or catalog exceptions freely, while boundary entry points catch and log them through the Lippincott handler.

```cpp title="targets/<name>/<name>.cpp"
#include <<name>/<name>.hpp>
#include <stdexcept>

namespace tle::<name> {

    namespace {
        void validate_inputs(std::span<int> values) {
            if (values.empty()) {
                throw std::invalid_argument("input span must not be empty");
            }
        }
    } // namespace

    auto process_data(std::span<int> values) noexcept -> bool {
        try {
            validate_inputs(values);
            for (auto& item : values) {
                item *= 2;
            }
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog);
        }
    }

} // namespace tle::<name>
```

Because `struct catalog` contains exactly one entry, `catch_boundary_exception(k_catalog)` selects that entry at compile time. For components with multiple catalog entries, pass the specific fallback entry such as `catch_boundary_exception(k_catalog.k_execution_failed)`.

To capture stack frames before unwinding begins, internal helpers can throw `journal::catalog_exception(k_catalog.k_execution_failed, "reason")`.

## Message catalog documentation

Document every catalog identifier in `targets/<name>/<name>.catalog`.

```text title="targets/<name>/<name>.catalog"
# systemd journald message catalog for component: <name>

-- a1b2c3d4e5f60718293a4b5c6d7e8f90
Subject: Workload execution failure caught at component boundary
Defined-By: tle::<name>
Support: https://github.com/JeffKarling/Target_Layout_Extended

The process_data boundary proxy caught an exception during internal workload
processing because the supplied span was empty or contained invalid data.
```

The header and catalog file together form the static and runtime contract of the component.

## Colocated unit test

Place functional verification in `targets/<name>/<name>TEST/`.

```cmake title="targets/<name>/<name>TEST/CMakeLists.txt"
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)
string(REGEX REPLACE "TEST$" "" COMPONENT_NAME ${DIR_NAME})

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

# Suppress Clang warning from GoogleTest static test registry macros
target_compile_options(${DIR_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-global-constructors>
)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::${COMPONENT_NAME}
        PkgConfig::GTEST
)

gtest_discover_tests(${DIR_NAME})
```

Write test cases in `<name>TEST.cpp` covering both normal operation and boundary error containment.

```cpp title="targets/<name>/<name>TEST/<name>TEST.cpp"
#include <<name>/<name>.hpp>
#include <gtest/gtest.h>
#include <vector>

namespace {

    namespace tested = tle::<name>;

    TEST(NameTest, ProcessesValidInputs) {
        std::vector<int> numbers{1, 2, 3};
        EXPECT_TRUE(tested::process_data(numbers));
        EXPECT_EQ(numbers.at(0), 2);
    }

    TEST(NameTest, CatchesInvalidInputsWithoutThrowing) {
        std::vector<int> empty{};
        EXPECT_FALSE(tested::process_data(empty));
    }

} // namespace
```

## Private helpers, angle bracket includes and splitting a new component

As a component grows, keep its physical boundary clean by following three rules for header inclusion and target splitting.

* **Angle bracket includes only.** Every `#include` across the repository uses angle brackets `<...>` rather than quotes `"..."`. Quoted includes resolve relative to the current source file on disk before consulting CMake target search paths, which can mask missing `target_link_libraries()` declarations or reach across directories via relative paths. Public headers always use the target prefix `#include <<name>/<name>.hpp>`, and private helpers inside the owning component use `#include <<helper>.hpp>`.
* **Private helpers stay in the component root.** When helper functions or classes are only needed inside a single component, place `<helper>.hpp` and `<helper>.cpp` at the root of `targets/<name>/` alongside `<name>.cpp`, and list `<helper>.cpp` under `PRIVATE` in `target_sources()`. Because CMake's `PUBLIC FILE_SET` defaults `BASE_DIRS` to `targets/<name>/` so `<name>/<name>.hpp` resolves, `FILES` in the `FILE_SET` controls installation and IDE header sets rather than hiding the root directory from linked consumers. Treat any unprefixed header outside `targets/<name>/<name>/` as strictly private to its owning component.
* **Shared capabilities become a new sibling target.** When a helper type or function is needed by a second component, split it into its own directory `targets/<new_name>/`. Give the new component its own public header, catalog and unit test suite. Never expose internal helpers by reaching across component directories.
* **Link time optimization removes boundary overhead.** Splitting code into fine grained static library targets does not penalize release performance. `cmake/templates/target_properties.cmake` enables `INTERPROCEDURAL_OPTIMIZATION` in release builds when `ENABLE_IPO` is on, allowing the linker to inline functions and deduplicate template instantiations across static archive boundaries.

## Verification workflow

After scaffolding the files, run verification across both GNU GCC and LLVM Clang.

```bash title="Run compiler verification suites"
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
```

Both compilers must compile and pass all tests before submitting code changes.
