[Base Target Layout](https://github.com/JeffKarling/Target_Layout) | [Base Documentation](https://jeffkarling.github.io/Target_Layout/) | [Why Target Layout](https://jeffkarling.github.io/Target_Layout/why_target_layout/)

# How it works

**Target Layout Extended** builds directly on the architectural foundation of **[Target Layout](https://jeffkarling.github.io/Target_Layout/)**. Before exploring the production extensions in this repository, it helps to review why the base layout exists and what core values it establishes.

## Core ideas from Target Layout

Traditional C++ layouts such as the Pitchfork Layout organize repositories by file extension across top level `include/`, `src/`, and `tests/` trees. Grouping files by artifact type rather than compilation boundaries scatters each component across three directory hierarchies and encourages coarse library targets.

**[Target Layout](https://jeffkarling.github.io/Target_Layout/)** combines John Lakos' physical component model from *Large Scale C++ Software Design* with the convention over configuration ergonomics of Rust's Cargo workspaces.

* **The component is the CMake target.** Every directory under `targets/` represents one physical component and one static CMake target.
* **One to one naming symmetry.** The component directory, CMake target, public header, translation unit, C++ namespace, and unit test share a single name derived automatically from the folder name via `${DIR_NAME}`.
* **Compiler enforced boundaries and cycle free design.** Components receive no include paths to sibling components unless they explicitly link them. Because every component is a static CMake target, CMake enforces an acyclic dependency graph at configure and link time.
* **Full context colocation.** Placing the public contract, private implementation, build script, and unit test in one folder lets human developers and AI coding assistants understand or refactor a component without searching across multiple directory trees.

For the full rationale behind physical component organization and delegated naming, see **[Why Target Layout](https://jeffkarling.github.io/Target_Layout/why_target_layout/)**.

## Anatomy of an extended component

Where the base project keeps each component as small as possible to teach the core naming rule, **Target Layout Extended** demonstrates how that same single folder boundary scales to production Linux engineering. Boundary exception handling, structured `systemd-journald` catalogs, isolated hardware counter benchmarks, score driven optimization baselines, static analysis, and runtime sanitizers all live inside the same component layout.

Consider the `database_manager` and `tbb_algos` components under `targets/`.

```text
targets/
├── database_manager/
│   ├── CMakeLists.txt
│   ├── database_manager.cpp
│   ├── database_helper.hpp
│   ├── database_helper.cpp
│   ├── database_manager.catalog
│   ├── database_manager/
│   │   └── database_manager.hpp
│   └── database_managerTEST/
│       ├── CMakeLists.txt
│       └── database_managerTEST.cpp
└── tbb_algos/
    ├── CMakeLists.txt
    ├── tbb_algos.cpp
    ├── tbb_algos.catalog
    ├── tbb_algos/
    │   └── tbb_algos.hpp
    ├── tbb_algosTEST/
    │   ├── CMakeLists.txt
    │   └── tbb_algosTEST.cpp
    └── tbb_algosBENCH/
        ├── CMakeLists.txt
        ├── tbb_algosBENCH.cpp
        └── baseline.json
```

Each file inside the component folder serves a distinct architectural role.

* **Public contract in `<name>/<name>.hpp`.** Exposed via CMake `FILE_SET TYPE HEADERS` so consumers write `#include <database_manager/database_manager.hpp>`. For service and subsystem boundaries, public functions are marked `noexcept` and expose narrow types alongside public log catalog identifiers. Data schema components and simple value types do not require `noexcept` wrappers.
* **Private implementation in `<name>.cpp`.** Implements the component logic and catches internal exceptions at the boundary proxy using a centralized Lippincott handler.
* **Private helpers in `<helper>.hpp` and `<helper>.cpp`.** Internal helper files such as `database_helper.hpp` sit at the root of the component folder outside the `PUBLIC FILE_SET`. They split internal implementation logic across manageable files without exposing those details to downstream targets.
* **Boundary log catalog in `<name>.catalog`.** A native `systemd-journald` message catalog documenting every `MESSAGE_ID` emitted when the component boundary catches an internal failure. Reading `<name>.hpp` alongside `<name>.catalog` gives humans and AI agents the static and runtime contract of the component.
* **Colocated unit test in `<name>TEST/`.** A dedicated GoogleTest executable target that derives its parent component name automatically from `${DIR_NAME}` and verifies functional behavior and boundary error handling in isolation.
* **Colocated benchmark in `<name>BENCH/`.** A dedicated hardware counter benchmark target that links `target_layout_extended::perf_scope` and evaluates component performance against the birth metrics and utility curve stored in `baseline.json`.

## Centralized target configuration without custom macros

In base **Target Layout**, compiler flags are kept minimal in the root `CMakeLists.txt`. In a larger project with custom debug profiles, standard library hardening, static analysis, sanitizers, and link time optimization, repeating flags in every component would duplicate configuration across directories.

Because every component binds its name to `${DIR_NAME}`, **Target Layout Extended** shares target properties and compiler options through two plain CMake scripts included by each component.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)
```

* **No custom wrapper functions.** Components still use standard `add_library()`, `target_sources()`, and `target_link_libraries()` calls rather than opaque wrapper macros.
* **Uniform properties across targets.** `target_properties.cmake` configures C++23 standard requirements, link time optimization via `INTERPROCEDURAL_OPTIMIZATION`, `clang-tidy`, `include-what-you-use`, `cppcheck`, and `OPTIMIZE_DEPENDENCIES`.
* **Generator expression flag matrices.** `target_compile_options.cmake` applies compiler flags, hardening macros, and sanitizer link flags per configuration across GCC and Clang.

## Code generation as a first class sibling component

Base **Target Layout** explains why code generation belongs in its own component directory. In **Target Layout Extended**, `targets/item_schema/` validates this pattern with Google Protobuf.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)

add_custom_command(
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DIR_NAME}/${DIR_NAME}.proto
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}/${DIR_NAME}.pb.cc
            ${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}/${DIR_NAME}.pb.h
        COMMENT "Generating ${DIR_NAME} C++ header and translation unit from ${DIR_NAME}.proto"
        COMMAND ${PROTOC_EXECUTABLE}
        ARGS --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/${DIR_NAME} ${DIR_NAME}.proto --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}
        VERBATIM
)

target_sources(${DIR_NAME}
        PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}/${DIR_NAME}.pb.cc

        PUBLIC
        FILE_SET ${DIR_NAME}
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${DIR_NAME}/${DIR_NAME}.pb.h
)

target_link_libraries(${DIR_NAME} PUBLIC PkgConfig::protobuf)
```

* **Schema input lives in version control.** The `.proto` file lives at `targets/item_schema/item_schema/item_schema.proto` while generated `.pb.h` and `.pb.cc` files stay inside `${CMAKE_CURRENT_BINARY_DIR}`.
* **Transparent `FILE_SET` consumption.** By setting `BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}` on the `PUBLIC FILE_SET`, downstream components like `database_manager` and `db_export` link `${PROJECT_NAME}::item_schema` and write `#include <item_schema/item_schema.pb.h>`.
* **Isolated warning and analysis policies.** Machine generated C++ code often triggers pedantic compiler or `clang-tidy` warnings. Keeping `item_schema` as its own component isolates static analysis suppression to the generated code while hand written components stay under strict checks.

## Executables follow the same naming rule

The composition root in `targets/app/` has no special naming exceptions.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::database_manager
        ${PROJECT_NAME}::tbb_algos
        ${PROJECT_NAME}::db_export
)
```

It derives `app` from its folder name, applies the shared target properties and compiler options, and links its direct dependencies privately.

## Architecture dependency graph export

Because every component is a one to one CMake target, `cmake/graphviz.cmake` exposes a built in `graphviz` target that exports the physical architecture of the repository.

```bash
cmake --build --preset GNU_Custom_Debug --target graphviz
```

This generates `production_artifacts/graphviz/project.dot` and renders `production_artifacts/graphviz/project.svg`. Both human engineers and AI coding assistants can inspect `project.dot` to see every component and its `PUBLIC` versus `PRIVATE` links in one place.
