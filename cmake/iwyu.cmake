include_guard(GLOBAL)

if(ENABLE_IWYU)
    find_program(IWYU_PATH NAMES include-what-you-use iwyu REQUIRED)
else()
    find_program(IWYU_PATH NAMES include-what-you-use iwyu)
endif()
