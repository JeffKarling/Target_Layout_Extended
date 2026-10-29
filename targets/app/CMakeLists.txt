cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

# An executable is a component too: directory name == target name (app).
# It has no public header because nothing links against it.
add_executable(${DIR_NAME} ${DIR_NAME}.cpp)

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

target_link_libraries(${DIR_NAME}
        PRIVATE
        ${PROJECT_NAME}::database_manager
        ${PROJECT_NAME}::tbb_algos
        ${PROJECT_NAME}::db_export
)
