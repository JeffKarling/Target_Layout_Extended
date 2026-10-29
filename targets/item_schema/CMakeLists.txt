cmake_path(GET CMAKE_CURRENT_LIST_DIR FILENAME DIR_NAME)

add_library(${DIR_NAME} STATIC)
add_library(${PROJECT_NAME}::${DIR_NAME} ALIAS ${DIR_NAME})

include(${CMAKE_BINARY_DIR}/target_properties.cmake)

# Generated Protobuf C++ sources use relaxed warning flags, isolated to this codegen target
# so hand-written components keep strict warning profiles.
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

# Disable clang-tidy and cppcheck on machine-generated protobuf sources
set_target_properties(${DIR_NAME} PROPERTIES
        CXX_CLANG_TIDY ""
        CXX_CPPCHECK ""
        CXX_INCLUDE_WHAT_YOU_USE ""
)

target_link_libraries(${DIR_NAME}
        PUBLIC
        PkgConfig::protobuf
)

if(BUILD_TESTING)
    add_subdirectory(${DIR_NAME}TEST)
endif()
