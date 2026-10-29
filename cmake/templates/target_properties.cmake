target_compile_features(${DIR_NAME} PRIVATE cxx_std_23)

set(IWYU_MAPPING_FILE "${CMAKE_SOURCE_DIR}/iwyu_mappings.imp")
set(IWYU_ARGS
        "${IWYU_PATH}$<SEMICOLON>-Xiwyu$<SEMICOLON>--mapping_file=${IWYU_MAPPING_FILE}"
)

set(CPPCHECK_ARGS
        "cppcheck$<SEMICOLON>--enable=all$<SEMICOLON>--std=c++23$<SEMICOLON>--inline-suppr$<SEMICOLON>--suppress=missingIncludeSystem$<SEMICOLON>--suppress=unusedStructMember"
)

set(IPO_VALUE "OFF")

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported)
    set(IPO_VALUE "$<$<AND:$<BOOL:${ENABLE_IPO}>,$<OR:$<CONFIG:Release>,$<CONFIG:GNU_Release>,$<CONFIG:Clang_Release>>>:ON>")
else()
    message(STATUS "IPO/LTO is not supported by the current compiler toolchain: ${ipo_error}")
endif()

set(WARNING_AS_ERROR_VALUE "OFF")
if(ENABLE_WARNING_AS_ERROR)
    set(WARNING_AS_ERROR_VALUE "$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:Clang_Debug>,$<CONFIG:GNU_Debug>,$<CONFIG:Clang_Tidy>,$<CONFIG:Clang_UBSan>,$<CONFIG:Clang_MSan>,$<CONFIG:Clang_TSan>,$<CONFIG:Clang_ASan>,$<CONFIG:Clang_LSan>,$<CONFIG:GNU_UBSan>,$<CONFIG:GNU_ASan>>,ON,OFF>")
endif()

set_target_properties(${DIR_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION "${IPO_VALUE}"

        CXX_INCLUDE_WHAT_YOU_USE "$<$<AND:$<BOOL:${ENABLE_IWYU}>,$<COMPILE_LANGUAGE:CXX>>:${IWYU_ARGS}>"
        CXX_CPPCHECK "$<$<AND:$<BOOL:${ENABLE_CPPCHECK}>,$<COMPILE_LANGUAGE:CXX>>:${CPPCHECK_ARGS}>"
        CXX_CLANG_TIDY "$<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<CONFIG:Clang_Tidy>>:clang-tidy;--checks=-*,modernize*,cppcoreguidelines*,bugprone*,clang-analyzer*,concurrency*,google*,readability*,performance*,-modernize-use-trailing-return-type>"

        EXPORT_COMPILE_COMMANDS ON
        DEBUG_POSTFIX _debug
        COMPILE_WARNING_AS_ERROR "${WARNING_AS_ERROR_VALUE}"
        CXX_EXTENSIONS OFF
        OPTIMIZE_DEPENDENCIES ON
)
