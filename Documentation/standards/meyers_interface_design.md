# Scott Meyers interface design rule

## Core mandate

Every public API, class and component boundary must follow the primary design guideline articulated by Scott Meyers. Make interfaces easy to use correctly and hard to use incorrectly.

The designer of an interface is accountable for client errors. If smart and conscientious developers make mistakes using an API, the interface design itself is defective.

If an attempted use of an interface would violate expectations, the code must not compile. If the code compiles, it must behave as the client expects.

## Strong typing by default

Express domain concepts in dedicated strong types instead of raw primitives.

Raw integers, floating point numbers and strings lack semantic intent. Using raw primitives invites primitive obsession and silent logic bugs.

Strong typing is recommended regardless of parameter count. A single parameter function like `set_timeout(Milliseconds ms)` prevents callers from mistakenly passing raw numbers or seconds. Similarly, `find_user(UserId id)` prevents callers from passing raw strings or email addresses.

Wrap primitive types in lightweight structs or scoped enums. Strong types turn conceptual mistakes into compile time errors.

## Restrict the value space and eliminate invalid states

Design types and signatures so that invalid states cannot be represented.

Use scoped enums rather than integers or boolean flags for modes, options, and status codes. Boolean flags in function signatures are ambiguous and error prone at call sites. Scoped enums make the intent explicit and self documenting.

Validate invariants at construction boundaries. Use private constructors paired with factory functions when initialization can fail. This ensures callers can only hold valid instances.

## Prevent argument transposition traps

Never place adjacent parameters sharing the same primitive or fundamental type in a signature.

Signatures taking multiple primitives of the same type invite silent transposition bugs. The compiler accepts transposed arguments without warnings, producing defects that escape static inspection.

When a function requires multiple inputs sharing the same underlying representation, strong types are mandatory.

## Resource safety through RAII

Never require the caller to remember cleanup steps, release locks, or free memory manually.

Resource acquisition must be tied to object lifetime. Functions that allocate resources must return RAII guards, smart pointers, or managed objects. Destructors must release resources deterministically.

Component boundaries must never return raw owning pointers.

## Consistency and least astonishment

Interfaces must behave consistently across components.

Follow standard C++ and standard library conventions for common operations. Maintain uniform parameter ordering across related functions within a subsystem.

Do not introduce surprising side effects in functions that appear to be queries. A getter must not alter observable state or perform unexpected heavy I/O operations.

## Explicit constructors and conversions

Mark single argument constructors and conversion operators explicit by default.

Implicit conversions allow the compiler to silently construct objects or convert unrelated types to satisfy a call. This hides bugs and subverts type safety.

Only omit explicit when designing intentional conversions for genuine mathematical or vocabulary types.

## Eliminate redundant caller burden

Do not ask the caller for information that the component, build system, or runtime environment can deduce safely.

If a component can determine ambient architectural context or deduce invariants internally, it must do so. Forcing callers to repeatedly provide boilerplate strings or redundant configuration creates opportunities for errors.

## Compile time enforcement over runtime checks

Push verification to the compiler whenever possible.

Prefer `static_assert`, concepts, and strong types over runtime assertions and error codes. A compile time failure stops the bug before the binary is built.

## Physical design simplicity

Keep public headers lightweight, clean, and macro free.

Complex deduction logic and heavy implementation details belong in private `.cpp` files. Public component headers must remain clean compilation firewalls following John Lakos physical design.

## Strict physical insulation and third party encapsulation

Public headers must never include third party headers or expose third party types.

John Lakos physical component design requires complete insulation between client translation units and private implementation dependencies. Exposing third party types or headers in `<name>/<name>.hpp` forces downstream consumers to inherit transitive compilation dependencies. This causes compilation cascades, leaks implementation details and couples client code to vendor ABIs.

All external dependencies must be wrapped behind standard C++ types such as `std::span`, `std::string_view` or dedicated strong types. External libraries must always be linked `PRIVATE` in `targets/<name>/CMakeLists.txt`.

Colocated unit test executables in `<name>TEST/` verify this boundary by linking only the component public interface without linking its private third party dependencies. If a public header accidentally leaks a private third party header, consumer compilation fails immediately.
