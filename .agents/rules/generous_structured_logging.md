# Generous structured logging and binary diagnostics

## Core mandate

Agents must log generously across components without fearing heap allocation overhead.

The `journal` backend formats structured fields into stack buffers using `constexpr` helpers and transmits them in a single vector call via `sd_journal_sendv`. Never withhold internal diagnostics under the mistaken assumption that logging slows down execution.

## All eight Syslog severity classifications

Use the strong scoped enumeration `journal::severity` covering all eight standard Syslog levels from `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, to `debug`.

Pass strong types `journal::severity`, `journal::message`, and `journal::binary_payload` in any order to `journal::log`. The component name is deduced automatically at compile time from the caller source location under the physical one name rule.

## Catalog entries strictly at component boundaries

Operational boundaries tie unhandled internal exceptions to documented catalog entries in `<name>.catalog` and `struct catalog`.

Use `journal::catch_boundary_exception` in boundary `catch` blocks and `journal::catalog_exception` when throwing specific contract failures internally. Every boundary failure event automatically attaches a binary `std::stacktrace` payload in `RAW_PAYLOAD=` alongside the invariant UUID `MESSAGE_ID`.

## Logging raw binary payloads

Take advantage of native binary logging capabilities when diagnosing state corruption.

Unlike text only loggers, the native wire protocol of `systemd-journald` accepts arbitrary length prefixed binary payloads via `sd_journal_sendv`. Wrap raw C++ structs, network packets, serialization headers, or memory snapshots using `journal::binary_payload` or `journal::as_binary_payload` without hex or base64 encoding overhead.

## Offline symbolization with addr2line

Binary stack traces attached in `RAW_PAYLOAD=` contain raw machine instruction pointers.

Do not attempt to demangle or symbolize stack traces at runtime inside component boundaries. Offline tools such as `addr2line` or `llvm-addr2line` map the captured 64 bit addresses back to source files and function names against the debug binary.
