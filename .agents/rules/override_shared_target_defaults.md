# Local overrides for shared target defaults

## Baseline inclusion mandate

Every physical component in this repository must inherit the shared global baseline files. This includes machine generated schemas, third party wrappers, and custom performance components.

Both `target_properties.cmake` and `target_compile_options.cmake` must always be included directly after the target declaration.

## Anti pattern of omitting shared includes

Omitting shared include files because of compilation issues is a severe anti pattern.

Dropping `target_compile_options.cmake` completely removes optimization levels, debug formats, standard library hardening, and sanitizer instrumentation. The component then becomes an invisible blind spot in the build matrix.

## The local override pattern

When a component encounters friction with global defaults, agents must retain the baseline files. Agents must apply minimal, surgical overrides directly on the specific target.

This practice preserves global optimizations, sanitizers, and hardening flags while addressing local technical constraints.

## Applying targeted warning suppressions

Machine generated code frequently conflicts with strict warning profiles. Google Protobuf schemas trigger warnings such as global constructors or sign conversions.

Agents must suppress only the exact offending diagnostics on the target using generator expressions.

```cmake
include(${CMAKE_BINARY_DIR}/target_properties.cmake)
include(${CMAKE_BINARY_DIR}/target_compile_options.cmake)

# Relax strict compiler warnings for machine generated Protobuf code while retaining project optimization and hardening defaults
target_compile_options(${DIR_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wno-global-constructors -Wno-conversion -Wno-sign-conversion>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-conversion -Wno-sign-conversion>
)
```

**Static analysis exclusions.** Disable clang-tidy, cppcheck, and include-what-you-use target properties when analyzing machine generated translation units.

```cmake
set_target_properties(${DIR_NAME} PROPERTIES
        SYSTEM ON
        CXX_CLANG_TIDY ""
        CXX_CPPCHECK ""
        CXX_INCLUDE_WHAT_YOU_USE ""
)
```

## Specialized performance adjustments

Specialized performance targets may require custom optimization flags or relaxed frame pointers.

Always inherit the shared compiler options first. Then apply local overrides with `target_compile_options` to tune the hot path.

Document the technical rationale for every local override directly above the target invocation.
