cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)
string(REGEX REPLACE "TEST$" "" COMPONENT_NAME ${DIR_NAME})

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::${COMPONENT_NAME}
        ${PROJECT_NAME}::database_manager
        PkgConfig::GTEST
)

gtest_discover_tests(${DIR_NAME})
