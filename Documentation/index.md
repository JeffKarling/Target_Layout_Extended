[Base Target Layout](https://github.com/JeffKarling/Target_Layout) | [Base Documentation](https://jeffkarling.github.io/Target_Layout/)

# Padded component anatomy

**Target Layout Extended** builds directly on the foundation established in **[Target Layout](https://jeffkarling.github.io/Target_Layout/)**. In the base repository, every directory under `targets/` represents one physical component and one CMake target where the directory, target, translation unit, public header, namespace, and unit test share a single name.

This extended template demonstrates what happens when that minimal skeleton is **padded for high velocity Linux production engineering**. Every production feature lives inside the exact same self contained component boundary.

## 1. Anatomy of a padded component

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

* **Public contract in `<name>/<name>.hpp`.** Exposed via CMake `FILE_SET TYPE HEADERS` so consumers write `#include <database_manager/database_manager.hpp>`. Public functions are marked `noexcept` and expose narrow types along with public log catalog identifiers.
* **Private implementation in `<name>.cpp`.** Implements the component logic and catches all internal exceptions at the boundary using a centralized Lippincott handler.
* **Private helpers in `<helper>.hpp` and `<helper>.cpp`.** Internal helper files such as `database_helper.hpp` sit at the root of the component folder outside the `PUBLIC FILE_SET`. They help split internal implementation logic across manageable files without exposing those details to downstream targets.
* **Boundary log catalog in `<name>.catalog`.** A native `systemd-journald` message catalog documenting every `MESSAGE_ID` emitted at the component boundary. Reading `<name>.hpp` alongside `<name>.catalog` gives humans and AI agents the complete static and runtime contract of the component.
* **Colocated unit test in `<name>TEST/`.** A dedicated GoogleTest executable target that derives its parent component name automatically from `${DIR_NAME}` and verifies functional behavior and boundary error handling in isolation.
* **Colocated benchmark in `<name>BENCH/`.** A dedicated hardware counter benchmark target that links `target_layout_extended::perf_scope` and evaluates component performance against the birth metrics and human utility curve stored in `baseline.json`.

## 2. Centralized target configuration without custom macros

In base **Target Layout**, compiler flags are kept minimal in the root `CMakeLists.txt`. In a larger project with custom debug profiles, standard library hardening, static analysis, sanitizers, and link time optimization, repeating flags in every component would violate DRY principles.

Because every component binds its name to `${DIR_NAME}`, **Target Layout Extended** shares all target properties and compiler options through two plain CMake scripts included by each component.

```cmake
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)
```

* **No custom wrapper functions.** Components still use standard `add_library()`, `target_sources()`, and `target_link_libraries()` calls rather than opaque `my_project_add_component()` macros.
* **Uniform properties across targets.** `target_properties.cmake` configures C++23 standard requirements, link time optimization via `INTERPROCEDURAL_OPTIMIZATION`, `clang-tidy`, `include-what-you-use`, `cppcheck`, and `OPTIMIZE_DEPENDENCIES`.
* **Generator expression flag matrices.** `target_compile_options.cmake` applies compiler flags, hardening macros, and sanitizer link flags per configuration across GCC and Clang.

## 3. Code generation as a first class sibling component

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

* **Schema input lives in version control.** The `.proto` file lives at `targets/item_schema/item_schema/item_schema.proto` while generated `.pb.h` and `.pb.cc` files stay strictly inside `${CMAKE_CURRENT_BINARY_DIR}`.
* **Transparent `FILE_SET` consumption.** By setting `BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}` on the `PUBLIC FILE_SET`, downstream components like `database_manager` and `db_export` simply link `${PROJECT_NAME}::item_schema` and write `#include <item_schema/item_schema.pb.h>`.
* **Isolated warning and analysis policies.** Machine generated C++ code often triggers pedantic compiler or `clang-tidy` warnings. Keeping `item_schema` as its own component isolates static analysis suppression to the generated code while hand written components stay under strict checks.

## 4. Executables follow the exact same rule

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

## 5. Architecture dependency graph export

Because every component is a one to one CMake target, `cmake/graphviz.cmake` exposes a built in `graphviz` target that exports the exact physical architecture of the repository.

```bash
cmake --build --preset GNU_Custom_Debug --target graphviz
```

This generates `production_artifacts/graphviz/project.dot` and renders `production_artifacts/graphviz/project.svg`. Both human engineers and AI coding assistants can inspect `project.dot` to see every component and its `PUBLIC` versus `PRIVATE` links in a single step.
