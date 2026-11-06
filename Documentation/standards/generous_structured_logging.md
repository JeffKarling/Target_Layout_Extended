# Generous structured logging and binary diagnostics

## Core mandate

Engineers must log generously across components without fearing heap allocation or I/O blocking on the calling thread.

The `journal` component records events into a preallocated 2048 slot `constinit` lock free multi producer single consumer ring buffer in roughly 30 nanoseconds. Producer calls capture a hardware cycle counter timestamp via `raw_timestamp`, static string pointers, and raw payload bytes without formatting strings or invoking system calls on the application thread. A dedicated `SCHED_BATCH` background consumer thread formats `iovec` fields into stack buffers and transmits them via `sd_journal_sendv`.

## Flight recorder mode and runtime policies

By default, `journal` operates as an in-memory flight recorder.

High volume `debug` logs remain inside the ring buffer with zero `systemd-journald` I/O traffic. When a record at or above `lookbehind_trigger` such as `error` or a boundary exception is processed, the consumer thread automatically flushes recent skipped `debug` breadcrumbs in sequence order with `LOG_LOOKBEHIND=1` ahead of the fault.

Runtime controllers can adjust per-level strides, congestion thresholds, or per-component `debug` microscope overrides at any time via `journal::activate_policy` and `journal::make_component_override`.

The `journal` target follows a strict last caller wins model. It must never embed IPC, networking, or authentication logic. External interfaces must reside in separate higher level components that link `${PROJECT_NAME}::journal`. Those adapters can expose POSIX signals or protocols like Varlink, D-Bus, and HTTP REST.

## All eight Syslog severity classifications

Use the strong scoped enumeration `journal::severity`. It covers all eight standard Syslog levels from `emergency` to `debug`.

Pass strong types `journal::severity`, `journal::message` and `journal::binary_payload` in any order to `journal::log`. The component name is deduced automatically at compile time from the caller source location under the physical one name rule.

## Catalog entries strictly at component boundaries

Operational boundaries tie unhandled internal exceptions to documented catalog entries in `<name>.catalog` and `struct catalog`.

Use `journal::catch_boundary_exception` in boundary `catch` blocks and `journal::catalog_exception` when throwing specific contract failures internally. Every boundary failure event automatically attaches a pre-unwind binary stack trace in `RAW_PAYLOAD=` alongside the invariant 32 character lowercase hexadecimal `MESSAGE_ID` and triggers a flight recorder lookbehind flush.

## Logging raw binary payloads

Take advantage of native binary logging capabilities when diagnosing state corruption.

Unlike text only loggers, the native wire protocol of `systemd-journald` accepts arbitrary length prefixed binary payloads via `sd_journal_sendv`. Wrap trivially copyable C++ structs, network packets or serialization buffers using `journal::as_binary_payload`. Raw `std::span<const std::byte>` views pass through without hex or base64 encoding overhead.

## Offline symbolization and unit test capture

Binary stack traces attached in `RAW_PAYLOAD=` contain raw 64 bit machine instruction pointers captured into fixed stack buffers.

Do not attempt to demangle or symbolize stack traces at runtime inside component boundaries. Offline tools such as `addr2line` or `llvm-addr2line` map the captured addresses back to source files and function names against the binary.

In unit tests that verify diagnostic emissions, redirect the consumer thread into memory with `journal::detail::set_record_sink`. Synchronize using `journal::flush` and restore the default sink with `journal::detail::reset_record_sink`.
