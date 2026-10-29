cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)
string(REGEX REPLACE "BENCH$" "" COMPONENT_NAME ${DIR_NAME})

add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_compile_definitions(${DIR_NAME} PRIVATE
        COMPONENT_BASELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/baseline.json"
        COMPONENT_REPORT_PATH="${PROJECT_SOURCE_DIR}/production_artifacts/profiling/${COMPONENT_NAME}_score_report.md"
)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::${COMPONENT_NAME}
        ${PROJECT_NAME}::perf_scope
)

add_test(NAME ${DIR_NAME} COMMAND ${DIR_NAME})

add_custom_target(${DIR_NAME}_run
        COMMAND $<TARGET_FILE:${DIR_NAME}>
        DEPENDS ${DIR_NAME}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running component benchmark ${DIR_NAME} and evaluating against baseline.json"
        VERBATIM
)
