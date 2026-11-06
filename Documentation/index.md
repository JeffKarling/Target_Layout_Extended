# How it works

Where **[Target Layout](https://jeffkarling.github.io/Target_Layout/)** provides the educational introduction to the physical component model, **Target Layout Extended** scales that model for high velocity software engineering. Every core rule remains in place. One directory under `targets/` represents one physical component and one static CMake target, with every file sharing the name derived from `${DIR_NAME}`.

When John Lakos formulated physical component design, software development moved at a deliberate human pace. In that era, public interface unit testing provided an adequate safety net.

Today, software iteration moves at unprecedented velocity. Rapid refactoring and modern engineering workflows modify complex systems within minutes. In this environment, functional unit tests alone are insufficient because they only verify logical pass or fail states. They remain completely blind to hidden performance degradation, unintended heap allocations, atomic contention and instruction bloat.

Target Layout Extended pairs every physical component with a dedicated performance baseline component in `<name>BENCH/`. By measuring hardware performance counters against committed baseline scorecards, the architecture provides objective mathematical guardrails that ensure high velocity changes never compromise production runtime performance.

This guide details the infrastructure required for production Linux engineering. Read [component boundaries and catalogs](component_boundaries.md) to explore the boundary failure contract, or follow [adding a component](adding_a_component.md) when scaffolding a new component. Explore [engineering standards](standards/index.md) for interface design, ranges usage and compiler policies.

## Prerequisites

The project targets Linux and resolves every third party dependency through `pkg-config` in `cmake/dependencies.cmake`.

* **Toolchain.** CMake 3.25 or newer, GCC and Clang with C++23 support. The `journal` component links `-lstdc++exp` for `std::stacktrace` via generator expression, so both compilers build against libstdc++ from GCC 14 or newer.
* **Libraries.** `protobuf` with a matching `protoc`, `tbb`, `libsystemd` and `gtest` when `BUILD_TESTING` is on.
* **Optional tools.** Use `perf` for profiling components and `dot` from Graphviz for dependency graphs. Static analysis uses `clang-tidy`, `include-what-you-use` and `cppcheck`. The build picks up `ccache` automatically when present.

## Anatomy of an extended component

The component folder stays the single unit of ownership. Everything the extension adds lives inside it. Consider the `database_manager` and `tbb_algos` components under `targets/`.

```text title="Extended component directory tree"
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

* **Public contract in `<name>/<name>.hpp`.** Exposed via CMake `FILE_SET TYPE HEADERS` so consumers write `#include <database_manager/database_manager.hpp>`. For service and subsystem boundaries, public functions are marked `noexcept` and the header declares the component's `struct catalog`. Data schema components and simple value types do not require `noexcept` wrappers.
* **Private implementation in `<name>.cpp`.** Implements the component logic and catches internal exceptions at the boundary proxy using a centralized Lippincott handler.
* **Private helpers in `<helper>.hpp` and `<helper>.cpp`.** Internal helper files such as `database_helper.hpp` sit at the root of the component folder outside the `PUBLIC FILE_SET` file list. Unlinked components receive no include path to the component folder, and every translation unit uses angle bracket `#include <...>` directives rather than quoted relative paths so all header resolution flows through CMake target search paths. Within the owning component, `<name>.cpp` includes its helper via `#include <database_helper.hpp>`, while external consumers only include `<name>/<name>.hpp`.
* **Boundary log catalog in `<name>.catalog`.** A native `systemd-journald` message catalog with one explanation per `MESSAGE_ID` declared in `struct catalog`. Reading the header alongside the catalog gives the static and runtime contract of the component.
* **Colocated unit test in `<name>TEST/`.** A GoogleTest executable that strips the `TEST` suffix from `${DIR_NAME}` to find its parent component. It verifies functional behavior and checks that boundary failures return cleanly.
* **Colocated benchmark in `<name>BENCH/`.** A hardware counter benchmark that links `perf_scope` and scores the component against `baseline.json`.

### The verification duo architecture

Every physical component pairs its functional contract with a verification duo. Together, `<name>TEST/` and `<name>BENCH/` guard both the logical correctness and the operational behavior of the component.

* **Dual contract verification.** The test component validates logical behavior, input edge cases and boundary failure firewalls using GoogleTest. The benchmark component validates physical runtime performance, throughput and hardware counter invariants using `perf_scope`.
* **Stress testing under sanitizers.** Because benchmarks register directly with CTest via `add_test()`, running the test suite executes high iteration workloads under active memory sanitizers. This catches subtle data races and memory leaks that brief unit tests never trigger.
* **Zero interface drift.** Both components act as pure consumers linking only the public header. Any breaking modification to the public contract fails compilation across tests and benchmarks synchronously.

## Centralized target configuration without custom macros

Repeating hardening, sanitizer, analysis and optimization flags in every component would duplicate configuration across directories. **Target Layout Extended** shares them through two plain CMake scripts that each component includes after creating its target.

```cmake title="targets/<name>/CMakeLists.txt"
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)
```

The scripts are not functions. They operate on whatever `${DIR_NAME}` holds at the point of inclusion, which works because the one name rule guarantees the variable names the current target. The sources live in `cmake/templates/` and `cmake/CMakeLists.txt` copies them into the build tree with `configure_file(... COPYONLY)`. That is why components include them from `${CMAKE_BINARY_DIR}`.

* **No custom wrapper functions.** Components still use standard `add_library()`, `target_sources()` and `target_link_libraries()` calls rather than opaque wrapper macros.
* **Uniform properties across components.** `target_properties.cmake` sets C++23 as a compile feature and turns on `POSITION_INDEPENDENT_CODE` and `OPTIMIZE_DEPENDENCIES`. It also wires up link time optimization and the optional `clang-tidy`, `include-what-you-use` and `cppcheck` hooks.
* **Generator expression flag matrices.** `target_compile_options.cmake` keys every flag set on `$<CONFIG:...>` with the custom build type names described in [presets and multi compiler workflow](presets_and_compilers.md).

## Code generation as a first class sibling component

Base **Target Layout** explains why code generation belongs in its own component directory. In **Target Layout Extended**, `targets/item_proto/` validates this pattern with Google Protobuf.

```cmake title="targets/item_proto/CMakeLists.txt"
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

# Relax strict compiler warnings for machine generated Protobuf code while retaining project optimization and hardening defaults
target_compile_options(${DIR_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-global-constructors -Wno-conversion -Wno-sign-conversion>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-conversion -Wno-sign-conversion>
)

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

set_target_properties(${DIR_NAME} PROPERTIES
        SYSTEM ON
        CXX_CLANG_TIDY ""
        CXX_INCLUDE_WHAT_YOU_USE ""
        CXX_CPPCHECK ""
)

target_link_libraries(${DIR_NAME} PUBLIC PkgConfig::protobuf)
```

* **Schema input lives in version control.** The `.proto` file lives at `targets/item_proto/item_proto/item_proto.proto` while generated `.pb.h` and `.pb.cc` files stay inside `${CMAKE_CURRENT_BINARY_DIR}`.
* **Transparent `FILE_SET` consumption.** By setting `BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}` on the `PUBLIC FILE_SET`, downstream components like `database_manager` and `db_export` link `${PROJECT_NAME}::item_proto` and write `#include <item_proto/item_proto.pb.h>`.
* **Inherited compile options with isolated warning suppressions.** The component includes both shared configuration files. It inherits all global optimization levels, debug formats, standard library hardening flags and sanitizer instrumentation. Targeted generator expressions suppress only the specific compiler warnings produced by machine generated code, while `SYSTEM ON` silences external header warnings in consumers.
* **Isolated code generation exception.** Dedicated code generation targets like Protobuf represent the sole exception where an external generator library is linked `PUBLIC` to export machine generated schema structs. Service and algorithm components encapsulate external libraries strictly as `PRIVATE`.

## Executables follow the same naming rule

The composition root in `targets/app/` has no special naming exceptions.

```cmake title="targets/app/CMakeLists.txt"
cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::database_manager
        ${PROJECT_NAME}::item_proto
        ${PROJECT_NAME}::tbb_algos
        ${PROJECT_NAME}::db_export
        ${PROJECT_NAME}::journal
)
```

It derives `app` from its folder name, applies the shared target properties and compiler options, and links its direct dependencies privately.

## Architecture dependency graph export

Because every component is a one to one CMake target, `cmake/graphviz.cmake` exposes a built in `graphviz` target that exports the physical architecture of the repository.

```bash title="Generate architecture graph"
cmake --build --preset GNU_Custom_Debug --target graphviz
```

This generates `production_artifacts/graphviz/project.dot` and renders `production_artifacts/graphviz/project.svg`. Both human engineers and AI coding assistants can inspect `project.dot` to see every component and its `PUBLIC` versus `PRIVATE` links in one place.
