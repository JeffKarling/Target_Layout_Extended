# Auto-register components: every subdirectory here is one physical component and one CMake target.
# Adding a component never requires editing this file or the root CMakeLists.txt.
file(GLOB COMPONENT_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(DIR ${COMPONENT_DIRS})
    if(IS_DIRECTORY ${DIR})
        add_subdirectory(${DIR})
    endif()
endforeach()
