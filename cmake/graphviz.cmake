include_guard(GLOBAL)

find_program(DOT_EXECUTABLE "dot")

if(DOT_EXECUTABLE)
    set(GRAPHVIZ_OUT_DIR "${PROJECT_SOURCE_DIR}/production_artifacts/graphviz")

    add_custom_target(graphviz
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GRAPHVIZ_OUT_DIR}"
            COMMAND ${CMAKE_COMMAND} "--graphviz=${GRAPHVIZ_OUT_DIR}/project.dot" "${PROJECT_SOURCE_DIR}"
            COMMAND "${DOT_EXECUTABLE}" -Tsvg -o "${GRAPHVIZ_OUT_DIR}/project.svg" "${GRAPHVIZ_OUT_DIR}/project.dot"
            COMMENT "Generating component dependency graph in production_artifacts/graphviz/project.svg"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            VERBATIM
    )
    message(STATUS "[DIAGRAM] Graphviz dot utility found: ${DOT_EXECUTABLE}")
else()
    message(STATUS "[DIAGRAM] Graphviz dot utility NOT found. graphviz target is disabled.")
endif()
