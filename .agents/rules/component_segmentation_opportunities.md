# Component segmentation opportunities

## Independent target isolation

Every component in Target Layout is an independent CMake target with strict private and public boundaries. Agents must view physical boundaries as enablers for incremental adoption and localized optimization.

## Private language standard adoption

Major standard library implementations maintain binary compatibility across modern language standards within the same toolchain.

When planning a component implementation, agents should consider adopting newer language features privately. A component can compile its internal translation unit under C++26 while presenting a C++23 header to consumers.

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

Agents must not apply aggressive performance flags globally when only specific algorithms require them.

Hot numerical routines or vector processing components can safely enable hardware specific SIMD extensions locally.

```cmake
# Isolate SIMD instruction sets to the hot compute kernel
target_compile_options(${DIR_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-mavx2 -mfma>
        $<$<CXX_COMPILER_ID:GNU>:-mavx2 -mfma>
)
```

Neighboring components remain safe from vectorization side effects or floating point precision loss.
