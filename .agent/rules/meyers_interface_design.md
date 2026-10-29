# Scott Meyers interface design rule

## Core mandate

Every public API, boundary proxy, and component interface must follow the primary design guideline articulated by Scott Meyers. Make interfaces easy to use correctly and hard to use incorrectly.

When designing or reviewing C++ interfaces in this project, agents must apply the following rules.

## Eliminate redundant parameters

Do not force the caller to supply information that the compiler or runtime already knows.

In a component architecture under the one name rule, the physical source location already identifies the owning component. Functions must deduce ambient architectural metadata from `std::source_location` rather than demanding explicit string literals at call sites.

If a parameter can be deduced or defaulted safely, eliminate it from the caller parameter list.

## Prevent argument transposition traps

Never place adjacent parameters sharing the same primitive or fundamental type in a signature.

Passing two `std::string_view` arguments side by side invites silent transposition bugs. The compiler accepts swapped arguments without warnings, causing silent runtime defects.

## Use strong types when multiple inputs are necessary

When a function genuinely requires multiple parameters sharing the same representation, introduce distinct strong types.

Wrap fundamental types in dedicated structs or scoped enums. Strong types turn argument transposition into compile time errors.

## Prevent copy and paste misattribution

Do not design interfaces that rely on human discipline to keep strings synchronized with physical architecture.

Requiring callers to pass component names as free text strings causes silent misattribution when code is copied across files. Interfaces must enforce physical invariants mechanically.

## Prefer standard vocabulary types for single inputs

When a function accepts a single string or slice, use standard `std::string_view`.

Do not wrap single inputs in artificial concepts like `std::convertible_to<std::string_view>` or redundant strong types. Single arguments cannot be transposed. Plain vocabulary types provide zero allocation efficiency without template bloat.

## Preserve physical design boundaries

Keep public headers lightweight and macro free.

Complex path deduction and error handling belong inside private implementation files. Public component headers must remain clean compilation firewalls following John Lakos physical design.
