# Clang-Tidy state log

This file records static analysis warnings from `Clang_Tidy` builds that fall into Category B situational design warnings and Category C stylistic noise. Category A safety critical issues are resolved immediately in code before committing.

Run static analysis using the dedicated CMake preset.

```bash
cmake --preset Clang_Tidy
cmake --build --preset Clang_Tidy
```

## Logged Category B and C warnings

* **Category B situational design warnings.**
  * `targets/journal/journal_sink.cpp` triggers `cppcoreguidelines-macro-usage` for `#define SD_JOURNAL_SUPPRESS_LOCATION 1` required by `<systemd/sd-journal.h>` before header inclusion.
  * `targets/journal/journal.cpp` triggers `cppcoreguidelines-pro-type-vararg` on Linux kernel `syscall` calls required for `SYS_gettid` thread identifier caching.
  * `targets/perf_scope/perf_scope.cpp` triggers `cppcoreguidelines-pro-type-vararg` on Linux kernel `syscall` and `ioctl` calls required for `perf_event_open` hardware counter management.
  * `targets/perf_scope/perf_scope.cpp` triggers `bugprone-easily-swappable-parameters` on internal anonymous namespace helper functions `open_hw_counter`, `extract_json_string`, and `extract_json_number`.
* **Category C stylistic noise.**
  * `targets/app/app.cpp` triggers `readability-magic-numbers` on the sample vector literal `{10, 20, 30, 40, 50, 60, 70, 80}`.
