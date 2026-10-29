cmake_minimum_required(VERSION 3.25)

project(target_layout_extended
        VERSION 1.1.0
        DESCRIPTION "Production C++ patterns built on Target Layout"
        HOMEPAGE_URL "https://github.com/JeffKarling/Target_Layout_Extended"
        LANGUAGES CXX
)

set(PROJECT_LICENSE "MIT-0")

option(ENABLE_WARNING_AS_ERROR "Treat compiler warnings as errors in debug and analysis builds" OFF)
option(ENABLE_IPO "Enable link time optimization for release builds" ON)
option(ENABLE_IWYU "Enable include-what-you-use static analysis" OFF)
option(ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(BUILD_TESTING "Build unit tests and component benchmarks" ON)

# Disable test target compilation only during dedicated Clang-Tidy static analysis passes.
# Sanitizer builds keep BUILD_TESTING enabled so ctest executes unit tests under ASan, UBSan, TSan, MSan, and LSan.
if(CMAKE_BUILD_TYPE MATCHES ".*Tidy.*")
    set(BUILD_TESTING OFF CACHE BOOL "Build unit tests and component benchmarks" FORCE)
endif()

if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(cmake)

if(BUILD_TESTING)
    include(GoogleTest)
endif()

# Every directory under targets/ is one physical component, which is one CMake target.
add_subdirectory(targets)
