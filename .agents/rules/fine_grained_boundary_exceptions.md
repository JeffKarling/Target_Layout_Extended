# Fine grained boundary exception rethrowing

## Minimum catch all baseline

Every operational component boundary requires at minimum one catch all exception handler.

This baseline guarantees that unexpected runtime failures never leak unhandled exceptions across physical target boundaries.

## Single rethrow as a code smell

Using only one generic exception rethrow across a component performing multiple distinct operations is a code smell.

A single blanket error ID obscures diagnostic context. It prevents callers and log aggregators from distinguishing between initialization errors, processing faults, and storage failures.

## Designing operation specific exception handlers

A well designed component defines fine grained exception rethrowing sites tailored to what the component actually does.

Each distinct public operation should capture exceptions within its own operational context. This provides unique catalog message IDs and explicit failure telemetry for every boundary path.

**Initialization and setup.** Catch startup errors such as invalid configuration or missing resources under a dedicated catalog entry.

**State modification and processing.** Catch runtime algorithm or data container faults under an operation specific catalog entry.

**Storage and serialization.** Catch serialization or disk write failures under an independent persistence catalog entry.

## Implementation pattern

Every fine grained catch block delegates to the shared Lippincott function using its specific catalog entry.

```cpp
auto database_manager::add_item(const item_proto::Item& item) noexcept -> bool {
    try {
        // Implementation specific logic
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(catalog::k_add_item_failed);
    }
}

auto database_manager::save() noexcept -> bool {
    try {
        // Implementation specific logic
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(catalog::k_save_failed);
    }
}
```

The underlying exception type and stack trace are preserved while attaching precise semantic context to system diagnostics.
