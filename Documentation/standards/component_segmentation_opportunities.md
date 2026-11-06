# Component segmentation opportunities

## Independent target isolation

Every component in Target Layout Extended is an independent CMake target with strict private and public boundaries. Engineers must view physical boundaries as enablers for incremental adoption, clean encapsulation and localized optimization.

## Private helpers versus splitting a new sibling component

Keep component boundaries sharp as responsibilities grow.

**Private internal helpers.** When helper functions or classes are only used inside a single component, place `<helper>.hpp` and `<helper>.cpp` at the root of `targets/<name>/` outside the `PUBLIC FILE_SET` directory `targets/<name>/<name>/`, and register `<helper>.cpp` under `PRIVATE` in `target_sources()`. Because CMake defaults the `FILE_SET` base directory to `targets/<name>/` so `<name>/<name>.hpp` resolves, linked consumers technically have `targets/<name>/` on their `-I` path. Encapsulation of `<helper>.hpp` therefore relies on two rules: quoted includes `#include "..."` are prohibited across the repository, and downstream consumers must only include target-prefixed `<name>/<name>.hpp` headers listed in `PUBLIC FILE_SET`.

**Splitting a new sibling target.** When a helper type, schema or algorithm is needed by a second component, split it into its own physical directory `targets/<new_name>/`. Give the new component its own public header, catalog and unit test suite. Never reach across component directories to share private files.

**Zero link time overhead in release builds.** Splitting functionality into fine grained static targets does not penalize release binaries because `target_properties.cmake` enables `INTERPROCEDURAL_OPTIMIZATION` when `ENABLE_IPO` is on, allowing the linker to inline across static archive boundaries.

## Private language standard adoption

Major standard library implementations maintain binary compatibility across modern language standards within the same toolchain.

When planning a component implementation, engineers should consider adopting newer language features privately. A component can compile its internal translation unit under C++26 while presenting a C++23 header to consumers.

```cmake
# Internal implementation uses modern language features without forcing upgrades on consumers
target_compile_features(${DIR_NAME} PRIVATE cxx_std_26)
```

Consumers link against the component without changing their own compile features.

## Scoped public standard upgrades

When public interfaces genuinely benefit from newer standard types, standard requirements propagate through public usage requirements.

Only components linking the target inherit the newer standard requirement. Unrelated branches of the dependency graph remain on their existing baseline standard.

```cmake
# Header contract introduces concepts or types requiring C++23
target_compile_features(${DIR_NAME} PUBLIC cxx_std_23)
```

## Localized compiler optimizations and hardware acceleration

Engineers must not apply aggressive performance flags globally when only specific algorithms require them.

Hot numerical routines or vector processing components can safely enable hardware specific SIMD extensions locally.

```cmake
# Isolate SIMD instruction sets to the hot compute kernel
target_compile_options(${DIR_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-mavx2 -mfma>
        $<$<CXX_COMPILER_ID:GNU>:-mavx2 -mfma>
)
```

Neighboring components remain safe from vectorization side effects or floating point precision loss.
