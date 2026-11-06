# Ranges and memoryless views

## Core mandate

Every sequence iteration, data filtering and element transformation must favor `<ranges>` and `std::views`. Legacy iterator pairs like `begin()` and `end()` or manual index loops must be replaced with range based alternatives.

Software should express what data transformation is happening rather than managing iteration bookkeeping. C++20 and C++23 range abstractions provide expressive, declarative pipelines with compile time type safety.

## Declarative memoryless pipelines

Compose sequence transformations with the pipe operator `|`.

Use `std::views::filter`, `std::views::transform`, `std::views::take` and `std::views::drop` instead of allocating temporary vectors. Range views are non-owning, lightweight objects that evaluate elements lazily on demand.

Never allocate an intermediate heap container merely to pass a subset or transformation to the next processing step.

## Universal range concepts in public signatures

Functions that inspect or process sequences must accept generic range concepts instead of concrete containers.

Accept `template <std::ranges::input_range R>` rather than `const std::vector<T>&`. This allows callers to pass standard vectors, stack arrays, subranges or filtered views without forcing heap allocations.

When contiguous memory and random access are strictly required, accept `std::span<T>` or constrain the parameter with `std::ranges::contiguous_range`.

## Projections over manual transformation loops

Range algorithms natively support projections. Use member projections instead of writing nested extraction loops or custom predicate wrappers.

Use `std::ranges::find` or `std::ranges::sort` directly with a projection argument such as `&Type::member`. Projections evaluate directly inside the algorithm, avoiding extra view wrappers or lambda clutter.

## Enumerate and iota over naked C style for loops

Naked C style index loops such as `for (std::size_t i = 0; i < size; ++i)` and `for (;;)` are prohibited across component implementations, tests and benchmarks.

When a numeric index is needed alongside element values, use `std::views::enumerate` with C++23 structured bindings `for (auto const [index, value] : std::views::enumerate(container))`.

When repeating an operation a fixed number of times or stepping across an integer interval without an underlying container, use `for (std::size_t const idx : std::views::iota(std::size_t{0U}, count))`. For unbounded event loops, write `while (true)` rather than `for (;;)`.

## Range algorithms and hoisting bounds checks under hardening

Custom release and debug presets compile with `_GLIBCXX_ASSERTIONS` and `_LIBCPP_HARDENING_MODE_FAST`. Calling `.at(idx)` or `.subspan(idx).front()` inside an index loop repeats bounds checking instructions on every iteration.

Prefer `std::ranges::transform`, `std::ranges::fold_left`, `std::ranges::to<std::vector<T>>` or `std::views::enumerate` over a pre-sliced `span.first(count)`. Hoisting the slice before the loop eliminates redundant per-element bounds checks while maintaining full memory safety.

## Lifetime safety and escaping views

Views never own the underlying data storage.

Never return a view over a local temporary container. If a view outlives the source container, iterating the view triggers undefined behavior due to dangling references.

When processed data must outlive the scope of a local container, materialize the pipeline into an owned container using `std::ranges::to` or standard container constructors.

## Concurrency and mutex boundaries

Never return a range view over data protected by an internal mutex across a member boundary.

Once a member function returns, any acquired `std::scoped_lock` releases its lock. Iterating a non-owning view outside the lock introduces immediate data race risks if another thread mutates the underlying container.

Across mutex boundaries, either return an owned detached container snapshot or provide an in-lock visitor method that yields a range view strictly during the lifetime of the lock.

## Parallel workloads and random access

Be mindful of iterator category requirements in parallel libraries like oneTBB.

Single pass views like `std::views::filter` cannot be split into independent workload chunks in constant time. Passing non-random-access views into parallel algorithms forces sequential traversal or defeats thread scheduling.

For parallel computing, use `std::span` or contiguous random access sequences to ensure work stealing can partition tasks efficiently.

## Pure predicates in pipeline adaptors

Predicates passed to `std::views::filter` must be pure, stateless functions.

Views evaluate lazily and may cache or re-evaluate elements during iteration. Avoid side effects, mutable lambda captures, or external state dependencies inside predicates.
