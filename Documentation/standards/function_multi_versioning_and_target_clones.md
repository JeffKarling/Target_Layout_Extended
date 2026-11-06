# Function multi versioning and target clones

## Core mandate

Performance critical routines must leverage `target_clones` when profiling reveals coarse compute bound bottlenecks over contiguous memory buffers. This enables monolithic static executables to deliver hardware accelerated SIMD throughput on modern processors while retaining universal baseline compatibility.

## Standard target progression

The attribute must specify the three standardized progression tiers.

```cpp
__attribute__((target_clones("default", "arch=x86-64-v3", "arch=x86-64-v4")))
```

* **Default fallback tier.** The keyword `default` is mandatory. It compiles against baseline x86-64, ensuring universal execution on any 64 bit Linux machine.
* **Mainstream acceleration tier.** The target `arch=x86-64-v3` enables 256 bit YMM registers alongside AVX2, FMA and BMI instructions. This tier provides the primary performance leap for data parallel algorithms.
* **Wide vector throughput tier.** The target `arch=x86-64-v4` enables 512 bit ZMM registers through AVX-512 foundation instructions. Modern processors such as AMD Zen 4, AMD Zen 5 and Intel Xeon execute this tier with high efficiency.

## Why tier v2 is omitted

Tier `arch=x86-64-v2` only introduces SSE4.2 and POPCNT while remaining constrained to 128 bit vector widths. For compute bound mathematical loops, v2 yields negligible throughput gains over baseline SSE2. Omitting v2 avoids generating redundant machine code and protects the instruction cache.

## Candidate qualification criteria

Indiscriminate cloning harms performance. Engineers and agents must verify that candidate routines satisfy strict technical criteria before applying the attribute.

* **Coarse computational granularity.** Cloned routines cannot be inlined at compile time because their target address resolves dynamically at runtime. The routine must contain enough computational work to easily amortize the IFUNC dispatch jump.
* **Compute bound execution profile.** Hardware performance counters captured via `perf_scope` must indicate high instructions per cycle and low cache misses. Memory bound algorithms stalled on memory latency gain no throughput from vectorization.
* **Linear memory access.** The algorithm must operate over contiguous sequences such as `std::span` buffers. Contiguous layouts enable the compiler auto-vectorizer to unroll arithmetic operations into SIMD pipelines.
* **Encapsulated kernel pattern.** Keep the multi versioned kernel private inside an anonymous namespace within the component implementation file. The public component API remains a clean standard signature.

## Verification across build presets

Custom presets like `GNU_Custom_Release` compile with `-march=native`, which already activates native host extensions across the entire binary. To verify that `target_clones` properly accelerates portable distribution builds, engineers must execute benchmarks and inspect symbols under default presets like `GNU_Default_Release`.

Inspect the compiled translation unit using `nm` to confirm that the dynamic linker IFUNC symbol and individual clone targets are generated correctly.

```bash
nm --demangle targets/<name>/CMakeFiles/<name>.dir/<name>.cpp.o | grep kernel
```
