list(APPEND CMAKE_MODULE_PATH
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/templates"
)

configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/target_properties.cmake"
        "${CMAKE_BINARY_DIR}/target_properties.cmake"
        COPYONLY
)
configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/target_compile_options.cmake"
        "${CMAKE_BINARY_DIR}/target_compile_options.cmake"
        COPYONLY
)

include(iwyu)
include(dependencies)
include(graphviz)
include(perf)
