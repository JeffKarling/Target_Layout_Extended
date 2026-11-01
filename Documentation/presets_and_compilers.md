# Presets and multi compiler workflow

C++ projects face a tension between local developer velocity and downstream consumer portability. Developers want host CPU optimizations, DWARF 5 debug symbols, frame pointers and strict warnings. Package maintainers and CI need neutral builds that compile on any x86-64 machine.

**Target Layout Extended** handles both with a dual preset strategy in `CMakePresets.json` across GNU GCC and LLVM Clang.

## Preset naming scheme

Every preset name follows one pattern.

```text
<Compiler>_<Flavor>_<Config>[_Test | _Verify]
```

| Part | Values | Meaning |
| --- | --- | --- |
| `Compiler` | `GNU`, `Clang` | Selects `g++` or `clang++` through the hidden `GNU` and `Clang` base presets |
| `Flavor` | `Custom`, `Default` | Project tuned flags or plain CMake build types |
| `Config` | `Debug`, `Debug_Deep`, `Release`, `RelWithDebInfo` | Optimization and checking level |
| Suffix | none, `_Test`, `_Verify` | Configure and build preset, test preset, or workflow preset |

Analysis presets drop the flavor part and use names such as `Clang_Tidy`, `Clang_ASan` and `GNU_UBSan`. Every configure preset writes to `Build/<presetName>`.

## Custom build types drive the flag matrix

Default presets set `CMAKE_BUILD_TYPE` to a standard value such as `Debug` or `Release`. Custom and analysis presets set it to a project specific name that encodes the compiler.

| Configure preset | `CMAKE_BUILD_TYPE` |
| --- | --- |
| `GNU_Default_Release` | `Release` |
| `GNU_Custom_Release` | `GNU_Release` |
| `Clang_Custom_Debug_Deep` | `Clang_Debug_Deep` |
| `Clang_ASan` | `Clang_ASan` |

`target_compile_options.cmake` and `target_properties.cmake` match these names with `$<CONFIG:GNU_Release>` and similar generator expressions. One flat list of generator expressions therefore covers every compiler and configuration without `if()` branches. Standard build types match none of the custom compile option expressions. Default presets therefore get CMake's stock flags, with link time optimization added in `Release` when `ENABLE_IPO` is on.

> [!NOTE]
> Because the build type names are custom, CMake does not add its own `-O` or `-g` defaults for them. Every flag a custom configuration needs is listed explicitly in `target_compile_options.cmake`.

## Custom development presets

Named `GNU_Custom_*` and `Clang_Custom_*`.

* **Host CPU instruction sets.** Every custom configuration passes `-march=native`. Release adds `-O3 -ffast-math -fstack-protector-strong` and link time optimization when `ENABLE_IPO` is on.
* **Accurate stack traces.** Debug configurations use `-O1` and `-O0` for GNU deep debug, plus `-ggdb -gdwarf-5 -fno-omit-frame-pointer -fno-optimize-sibling-calls`.
* **Strict warnings in debug.** `Debug` and `Debug_Deep` enable `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2` plus compiler specific diagnostics. Release configurations do not repeat the warning set.
* **Standard library hardening.** Each configuration defines its own libstdc++ or libc++ hardening macros. See [static analysis and deep debug](static_analysis_and_debug.md).

> [!WARNING]
> `-ffast-math` in `*_Custom_Release` and `Clang_Custom_RelWithDebInfo` relaxes IEEE 754 semantics. NaN and infinity checks may be optimized away and floating point results can differ from Default builds. Remove the flag for components that depend on strict floating point behavior.

Set `-DENABLE_WARNING_AS_ERROR=ON` at configure time to turn warnings into errors in debug, analysis and sanitizer configurations.

## Default portable presets

Named `GNU_Default_*` and `Clang_Default_*`.

* **Standard CMake flags.** Use the built in `Debug`, `Release` and `RelWithDebInfo` build types without `-march=native` or project warning flags.
* **Distribution readiness.** Confirm that external users and CI pipelines can build the repository on any standard Linux machine. Binaries from Custom presets may not run on an older CPU.

## Why verify across both GNU GCC and LLVM Clang

Compiling and testing every component under both `g++` and `clang++` catches defects that a single compiler misses.

* **Distinct front end diagnostics.** Clang flags exit time destructors, global constructors and padded bitfields. GCC catches duplicated conditions, string operation overflow and null dereference paths.
* **Independent optimizer passes.** Code that accidentally relies on undefined behavior often passes under one optimizer and fails unit tests under the other.
* **Complementary checking support.** GCC provides full iterator tracking through `_GLIBCXX_DEBUG_PEDANTIC`. Clang provides the MemorySanitizer, ThreadSanitizer and standalone LeakSanitizer presets.

## Workflow presets and build_all.sh

CMake workflow presets chain configure, build and `ctest` into one command for a single toolchain.

```bash
cmake --workflow --preset GNU_Custom_Debug_Verify
cmake --workflow --preset Clang_Custom_Debug_Verify
```

A workflow preset cannot span two configure presets. `production_artifacts/build_all.sh` therefore runs the GNU workflow followed by the Clang workflow and stops at the first failure.

```bash
./production_artifacts/build_all.sh custom debug
./production_artifacts/build_all.sh default release
./production_artifacts/build_all.sh custom deep
```

The first argument picks the flavor and the second picks `debug`, `deep` or `release`. Running the script without arguments verifies `custom release`.
