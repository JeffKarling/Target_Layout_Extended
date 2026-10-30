# Generous structured logging and binary diagnostics

## Core mandate

Agents must log generously across components without fearing runtime overhead.

Generating a log event in application code is nearly free. It performs a lock free write of raw values and compile time pointers into a preallocated memory slot.

Never withhold diagnostics under the mistaken assumption that logging slows down critical execution.

## Standard severity classifications

Use standard severity levels including `debug`, `info`, `warning`, `error`, and `critical`.

Tag each log event with its natural severity level. Decisions regarding filtering, sampling, or post processing are handled asynchronously inside the logging component.

Under the default flight recorder policy, even verbose `debug` events stay strictly in a preallocated RAM ring buffer. They incur zero disk I/O cost unless a fault triggers a lookbehind flush.

## Structured catalog entries at component boundaries

Operational boundaries must tie diagnostic and error events to documented catalog entries.

Use catalog entries defined in `<name>.catalog` for boundary failures. Every catalog event carries an invariant UUID message identifier, component name, and human readable summary.

This allows automated log aggregators and systemd journald to index events deterministically across deployments.

## Logging raw binary payloads

Take advantage of native binary logging capabilities when diagnosing state corruption.

Unlike traditional text only loggers, the native wire protocol of `systemd-journald` accepts arbitrary length prefixed binary payloads. These payloads are passed as `std::span<const std::byte>` without hex or base64 encoding overhead.

Attach raw C++ structs, network packets, serialization headers, or memory snapshots directly to log entries during error investigations.
