cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

# Implementation uses C++23 std::ranges::to
target_compile_features(${DIR_NAME} PRIVATE cxx_std_23)

target_sources(${DIR_NAME}
        PRIVATE
        ${DIR_NAME}.cpp
        database_helper.cpp

        PUBLIC
        FILE_SET ${DIR_NAME}
        TYPE HEADERS
        FILES
        ${DIR_NAME}/${DIR_NAME}.hpp
)

target_link_libraries(${DIR_NAME}
        PUBLIC
        ${PROJECT_NAME}::item_proto
        ${PROJECT_NAME}::journal
)

if(BUILD_TESTING)
    add_subdirectory(${DIR_NAME}TEST)
endif()
