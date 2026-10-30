# Boundary exceptions and informative catalog logging

## Rethrowing more than one exception is a code smell

Every physical component in this repository must maintain a clean boundary contract.

Rethrowing more than one exception across a component boundary is a code smell. A focused physical component must handle its own domain logic internally, exposing at most a single boundary catch all firewall.

## Exceptions steer application flow

Exceptions act like road crossing traffic lights. They halt execution and steer application control flow.

When a component exposes multiple exception pathways, it creates tight symbiosis between caller and callee. The upstream caller becomes tightly coupled to callee internal failure hierarchies.

If an attempted design requires multiple distinct exception types at the boundary, the component is likely doing too much. The solution is splitting the component into smaller targets, not multiplying boundary exceptions.

## Informative catalog logging is not control flow

While exposing multiple exceptions is an anti pattern, defining multiple public catalog log entries is encouraged.

Catalog logs are purely informative. They never alter application control flow or burden upstream callers. Upstream consumers inspect return values rather than intercepting log telemetry.

Adding multiple catalog log entries represents good design. Catalog entries can span all standard severity levels from debug to critical, providing structured observability for systemd journald.

## Catalogs as public interface documentation

Message catalogs serve as the public interface documentation for every physical component.

By reading public catalog definitions, the reader should understand the component's high level features and capabilities without reading the actual code. The more catalog entries used across the component, the better the reader understands what the component is doing.

Catalogs form an index of capabilities, state transitions, and diagnostic checkpoints. While exception throwing must remain strictly minimal, catalog definitions should be expansive across all standard severity classifications.

## Rich catalog entries across severity levels

A cohesive component can define multiple catalog entries across standard severity classifications.

**Informational lifecycle milestones.** Log normal state transitions, such as completing initialization or processing a major batch.

**Warning thresholds.** Log degraded operational conditions, retry events, or cache capacity warnings.

**Boundary fault telemetry.** Pair the single boundary exception firewall tightly with a documented error or critical catalog entry.

## Implementation pattern

The single boundary exception handler catches unexpected faults, logs against the failure catalog entry, and returns a value based status. Informative catalog entries log operational telemetry during normal execution.

```cpp
auto database_manager::add_item(const item_proto::Item& item) noexcept -> bool {
    try {
        // Implementation logic
        journal::log_info(catalog::k_item_added, item.name());
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(catalog::k_boundary_failure);
    }
}
```

This architecture keeps components loosely coupled while delivering deep operational observability.
