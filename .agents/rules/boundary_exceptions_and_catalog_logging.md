# Boundary exceptions and catalog contract logging

## Component boundaries catch exceptions and return boolean status

Every operational and subsystem component in this repository must maintain a clean `noexcept` boundary contract.

Exceptions must never escape across a component boundary. The public interface catches every unhandled exception and converts it into a boolean `false` or `std::nullopt`. A focused operational component handles recoverable domain conditions internally and shields upper layers from callee failure mechanisms. Pure data schema components such as generated Protobuf targets and fine grained value types follow standard C++ value semantics without boundary exception firewalls.

## Decoupling control flow from failure root causes

When a component exposes multiple exception pathways or detailed error enums in its return type, it creates tight symbiosis between caller and callee. The upstream caller becomes coupled to callee internal failure hierarchies.

If an upstream caller genuinely needs to branch on two different conditions, the component should split the operation into separate functions or split into smaller targets. Once a boundary function gives up, the upstream C++ caller only needs `false` or `std::nullopt` to steer control flow, while `systemd-journald` receives the exact failure root cause and flushed lookbehind breadcrumbs.

## Catalogs represent boundary contract failures

Message catalogs in `<name>.catalog` and `struct catalog` document the public boundary failure contracts of a physical component.

Catalog entries are reserved strictly for `noexcept` boundary failures handled by `journal::catch_boundary_exception` and `journal::catalog_exception`. Routine internal component diagnostics use `journal::log` with strong types and never require catalog UUIDs.

## Automatic deduction for single entry components and explicit selection for multi entry components

Each operational component defines a closed `struct catalog` aggregate with at most eight `journal::catalog_entry` members and `inline constexpr catalog k_catalog{};` in its public header.

**Single entry components.** When `struct catalog` contains only one `catalog_entry`, the `journal::single_entry_catalog` concept is satisfied at compile time. The boundary `catch` block calls `journal::catch_boundary_exception(k_catalog)` without naming the individual entry.

**Multi entry components.** When `struct catalog` contains multiple `catalog_entry` fields, the compiler rejects `journal::catch_boundary_exception(k_catalog)`. Each boundary `catch` block must explicitly pass a fallback `catalog_entry`. Any standard exception or third party error caught at the boundary logs against this fallback entry. To attribute a specific failure catalog entry instead of the fallback, internal helpers throw `journal::catalog_exception`. This also freezes up to 32 raw stack frame addresses at the throw point before stack unwinding begins.

## Supporting unexpected fault nets and granular internal failure paths

This architecture accommodates two distinct exception philosophies without compromising the public contract.

**Unexpected fault safety nets.** When exceptions represent only unexpected bugs or environment failures, a component defines a single catalog entry. Standard code paths return domain values, and the boundary firewall safely intercepts any unexpected exception that slips through.

**Granular internal failure routing.** When deep internal helpers must abort on specific contract violations, they throw `journal::catalog_exception`. This avoids threading error codes across helper layers. The exact failure UUID routes directly to the boundary, and raw stack frames freeze before unwinding begins.

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
