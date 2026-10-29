include_guard(GLOBAL)

find_package(PkgConfig GLOBAL REQUIRED)

pkg_check_modules(protobuf GLOBAL REQUIRED IMPORTED_TARGET protobuf)

# Prefer the system protoc matching the pkg-config protobuf library
find_program(PROTOC_EXECUTABLE NAMES protoc PATHS /usr/bin NO_DEFAULT_PATH)
if(NOT PROTOC_EXECUTABLE)
    find_program(PROTOC_EXECUTABLE NAMES protoc REQUIRED)
endif()

pkg_check_modules(tbb GLOBAL REQUIRED IMPORTED_TARGET tbb)
pkg_check_modules(libsystemd GLOBAL REQUIRED IMPORTED_TARGET libsystemd)

if(BUILD_TESTING)
    pkg_check_modules(GTEST GLOBAL REQUIRED IMPORTED_TARGET gtest gtest_main)
    if(TARGET PkgConfig::GTEST)
        target_compile_options(PkgConfig::GTEST INTERFACE
                $<$<CXX_COMPILER_ID:Clang>:-Wno-global-constructors>
        )
    endif()
endif()
