# Boundary exceptions and catalog contract logging

## Rethrowing more than one exception across boundaries is a code smell

Every physical component in this repository must maintain a clean `noexcept` boundary contract.

Rethrowing exceptions across a component boundary is a code smell. A focused physical component handles recoverable domain conditions internally and exposes a `noexcept` boundary returning `bool` or `std::optional`.

## Decoupling control flow from failure root causes

When a component exposes multiple exception pathways or detailed error enums in its return type, it creates tight symbiosis between caller and callee. The upstream caller becomes coupled to callee internal failure hierarchies.

If an upstream caller genuinely needs to branch on two different conditions, the component should split the operation into separate functions or split into smaller targets. Once a boundary function gives up, the upstream C++ caller only needs `false` or `std::nullopt` to steer control flow, while `systemd-journald` receives the exact failure root cause.

## Catalogs represent boundary contract failures

Message catalogs in `<name>.catalog` and `struct catalog` document the public boundary failure contracts of a physical component.

Catalog entries are reserved strictly for `noexcept` boundary failures handled by `journal::catch_boundary_exception` and `journal::catalog_exception`. Routine internal component diagnostics use `journal::log` with strong types and never require catalog UUIDs.

## Automatic deduction for single entry components and explicit selection for multi entry components

Each component defines a closed `struct catalog` aggregate and `inline constexpr catalog k_catalog{};` in its public header.

**Single entry components.** When `struct catalog` contains only one `catalog_entry`, the `journal::single_entry_catalog` concept is satisfied at compile time. The boundary `catch` block calls `journal::catch_boundary_exception(k_catalog)` without naming the individual entry.

**Multi entry components.** When `struct catalog` contains multiple `catalog_entry` fields, the compiler rejects `journal::catch_boundary_exception(k_catalog)`. Each boundary `catch` block must explicitly pass its fallback `catalog_entry`, and internal helpers can throw `journal::catalog_exception` to select a specific `catalog_entry` and capture the binary `std::stacktrace` before stack unwinding begins.

## Implementation pattern

```cpp
// Small component with a single catalog entry
auto parallel_double(std::span<int> numbers) noexcept -> bool {
    try {
        run_parallel_work(numbers);
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(k_catalog);
    }
}

// Larger component with multiple catalog entries
auto database_store::add_item(const Item& item) noexcept -> bool {
    try {
        if (item.id().empty()) {
            throw journal::catalog_exception(k_catalog.k_add_item_failed, "item id must not be empty");
        }
        insert_internal(item);
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(k_catalog.k_add_item_failed);
    }
}
```

This architecture keeps components loosely coupled while delivering pre unwind binary stacktraces and structured contract observability.
