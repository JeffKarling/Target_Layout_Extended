include_guard(GLOBAL)

find_program(PERF_EXECUTABLE "perf")

if(PERF_EXECUTABLE)
    set(PERF_OUT_DIR "${PROJECT_SOURCE_DIR}/production_artifacts/profiling/perf")
    file(MAKE_DIRECTORY "${PERF_OUT_DIR}")

    # Target 1: Hardware counter statistics saved to console and file
    add_custom_target(Perf_stat
            COMMAND /bin/sh -c "\"${PERF_EXECUTABLE}\" stat -e task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses \"$<TARGET_FILE:app>\" 2>&1 | tee \"${PERF_OUT_DIR}/perf_stat_report.txt\""
            COMMENT "Running perf stat on app and writing production_artifacts/profiling/perf/perf_stat_report.txt"
            DEPENDS app
            WORKING_DIRECTORY "${PERF_OUT_DIR}"
            VERBATIM
    )

    # Target 2: Call-graph sampling recording
    add_custom_target(Perf_record
            COMMAND ${CMAKE_COMMAND} -E rm -f "${PERF_OUT_DIR}/perf.data"
            COMMAND "${PERF_EXECUTABLE}" record -g --call-graph dwarf -F 999 -o "${PERF_OUT_DIR}/perf.data" "$<TARGET_FILE:app>"
            COMMENT "Recording DWARF call-graph profile to production_artifacts/profiling/perf/perf.data"
            DEPENDS app
            WORKING_DIRECTORY "${PERF_OUT_DIR}"
            VERBATIM
    )

    # Target 3: Non-interactive text hotspot summary for developers and AI agents
    add_custom_target(Perf_report
            COMMAND /bin/sh -c "\"${PERF_EXECUTABLE}\" report --stdio --no-children --percent-limit 0.5 -i \"${PERF_OUT_DIR}/perf.data\" > \"${PERF_OUT_DIR}/perf_hotspot_report.txt\" && cat \"${PERF_OUT_DIR}/perf_hotspot_report.txt\""
            COMMENT "Generating non-interactive hotspot summary in production_artifacts/profiling/perf/perf_hotspot_report.txt"
            DEPENDS app
            WORKING_DIRECTORY "${PERF_OUT_DIR}"
            VERBATIM
    )

    # Target 4: Non-interactive assembly and source line annotation
    add_custom_target(Perf_annotate
            COMMAND /bin/sh -c "\"${PERF_EXECUTABLE}\" annotate --stdio -l -i \"${PERF_OUT_DIR}/perf.data\" > \"${PERF_OUT_DIR}/perf_annotate_report.txt\""
            COMMENT "Generating instruction-level annotation in production_artifacts/profiling/perf/perf_annotate_report.txt"
            DEPENDS app
            WORKING_DIRECTORY "${PERF_OUT_DIR}"
            VERBATIM
    )

    message(STATUS "[PROFILING] Linux perf tool found at: ${PERF_EXECUTABLE}")
else()
    message(STATUS "[PROFILING] Linux perf tool NOT found.")
endif()
