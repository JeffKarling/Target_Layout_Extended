target_compile_definitions(${DIR_NAME} PRIVATE
        # Clang configurations
        $<$<CONFIG:Clang_Debug>:
            _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG
            _GLIBCXX_ASSERTIONS
        >
        $<$<CONFIG:Clang_Debug_Deep>:
            _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
            _GLIBCXX_ASSERTIONS
        >
        $<$<CONFIG:Clang_Release>:NDEBUG _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>
        $<$<CONFIG:Clang_RelWithDebInfo>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE>
        $<$<CONFIG:Clang_Tidy>:NDEBUG>
        $<$<CONFIG:Clang_UBSan>:NDEBUG>
        $<$<CONFIG:Clang_MSan>:NDEBUG>
        $<$<CONFIG:Clang_TSan>:NDEBUG>
        $<$<CONFIG:Clang_ASan>:NDEBUG>
        $<$<CONFIG:Clang_LSan>:NDEBUG>

        # GNU GCC configurations
        $<$<CONFIG:GNU_Debug>:_GLIBCXX_ASSERTIONS DEBUG_LEVEL=2>
        $<$<CONFIG:GNU_Debug_Deep>:_GLIBCXX_DEBUG _GLIBCXX_DEBUG_PEDANTIC DEBUG_LEVEL=2>
        $<$<CONFIG:GNU_Release>:NDEBUG _GLIBCXX_CONCEPT_CHECKS _GLIBCXX_ASSERTIONS>
        $<$<CONFIG:GNU_RelWithDebInfo>:_GLIBCXX_CONCEPT_CHECKS _GLIBCXX_ASSERTIONS>
        $<$<CONFIG:GNU_UBSan>:NDEBUG>
        $<$<CONFIG:GNU_ASan>:NDEBUG>
)

target_compile_options(${DIR_NAME} PRIVATE
        # Clang custom build options
        $<$<CONFIG:Clang_Release>:-march=native -O3 -ffast-math -fstack-protector-strong>
        $<$<CONFIG:Clang_Debug>:-march=native -O1 -ggdb -gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls
        -Wall -Wextra -Wpedantic -Wswitch-enum -Wmissing-field-initializers -Wshadow -Wfatal-errors -Werror=return-type -Wconversion -Wformat=2
        -Wexit-time-destructors -Wglobal-constructors -Wpessimizing-move -Wrange-loop-construct -Wpadded-bitfield>
        $<$<CONFIG:Clang_Debug_Deep>:-march=native -O1 -ggdb -gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls
        -Wall -Wextra -Wpedantic -Wswitch-enum -Wmissing-field-initializers -Wshadow -Wfatal-errors -Werror=return-type -Wconversion -Wformat=2
        -Wexit-time-destructors -Wglobal-constructors -Wpessimizing-move -Wrange-loop-construct -Wpadded-bitfield>
        $<$<CONFIG:Clang_RelWithDebInfo>:-march=native -O3 -ffast-math -ggdb -gdwarf-5 -gline-tables-only -fdebug-info-for-profiling -fno-omit-frame-pointer>
        $<$<CONFIG:Clang_Tidy>:-march=native -O2 -g>

        # Clang runtime sanitizers
        $<$<CONFIG:Clang_UBSan>:-march=native -O2 -ggdb -gdwarf-5 -fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls>
        $<$<CONFIG:Clang_MSan>:-march=native -O2 -g -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -fno-optimize-sibling-calls>
        $<$<CONFIG:Clang_TSan>:-march=native -O2 -g -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls>
        $<$<CONFIG:Clang_ASan>:-march=native -O2 -g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls>
        $<$<CONFIG:Clang_LSan>:-march=native -O2 -g -fsanitize=leak -fPIE -fno-omit-frame-pointer -fno-optimize-sibling-calls>

        # GNU GCC custom build options
        $<$<CONFIG:GNU_Release>:-march=native -O3 -ffast-math -fstack-protector-strong>
        $<$<CONFIG:GNU_Debug>:-march=native -O1 -ggdb -gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 -Wduplicated-cond -Wstringop-overflow -Wformat-security
        -Wfloat-equal -Wlogical-not-parentheses -Wnull-dereference
        -Wpessimizing-move -Wrange-loop-construct>
        $<$<CONFIG:GNU_Debug_Deep>:-march=native -O0 -ggdb -gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 -Wduplicated-cond -Wstringop-overflow -Wformat-security
        -Wfloat-equal -Wlogical-not-parentheses -Wnull-dereference
        -Wpessimizing-move -Wrange-loop-construct>
        $<$<CONFIG:GNU_RelWithDebInfo>:-march=native -O2 -g -fno-omit-frame-pointer>
        $<$<CONFIG:GNU_UBSan>:-march=native -O2 -g -fsanitize=undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls>
        $<$<CONFIG:GNU_ASan>:-march=native -O2 -g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls>
)

target_link_options(${DIR_NAME} PRIVATE
        $<$<CONFIG:Clang_UBSan>:-fsanitize=undefined -fno-sanitize-recover=undefined>
        $<$<CONFIG:Clang_MSan>:-fsanitize=memory -fsanitize-memory-track-origins=2>
        $<$<CONFIG:Clang_TSan>:-fsanitize=thread -pie>
        $<$<CONFIG:Clang_ASan>:-fsanitize=address>
        $<$<CONFIG:Clang_LSan>:-fsanitize=leak -pie>
        $<$<CONFIG:GNU_UBSan>:-fsanitize=undefined>
        $<$<CONFIG:GNU_ASan>:-fsanitize=address>
)
