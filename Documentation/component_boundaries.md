# Component boundaries and catalogs

In a system built around physical components, the boundary between components acts as an architectural wall. A component consumer only sees `<name>/<name>.hpp` and links `${PROJECT_NAME}::<name>`. Everything else stays private inside `targets/<name>/`.

**Target Layout Extended** demonstrates two runtime practices at service and subsystem walls. First, exceptions thrown inside private internals are caught at the component boundary proxy before crossing into another CMake target. Second, boundary failures and internal diagnostics are recorded to `systemd-journald` through `targets/journal/`.

## Architectural rationale and why not standard text logging

A developer encountering this architecture might naturally ask why the project does not simply use `spdlog` or plain text syslog. Standard text logging frameworks are familiar, but they conflict with John Lakos physical component boundaries in three fundamental ways.

### The breakdown of traditional logging in component architectures

**Unbounded runtime string allocations.** Libraries like `spdlog` format arbitrary strings on the heap inside failing processes. When a failure stems from memory exhaustion or corrupt state, heap formatting in the failure path is unreliable. It can easily induce secondary crashes.

**Absence of physical contracts.** Standard loggers have no concept of a physical component contract. Developers sprinkle ad hoc log strings across private implementation files. This leaves callers and operators with no documented failure taxonomy.

**Violating boundary decoupling.** To communicate root causes to upstream callers, traditional designs either propagate exception hierarchies or return elaborate error enums. Both practices violate Lakos principles by creating tight physical coupling between callers and callee internal error types.

### The symbiosis of journald and physical component boundaries

`systemd-journald` is not an arbitrary logging sink in this repository. It provides the exact operating system primitives required to enforce physical component isolation.

Message catalogs are a native feature of `systemd-journald`, not an internal naming convention invented by this project. In standard systemd, a message catalog is an indexed database mapping 128 bit `MESSAGE_ID` UUIDs to diagnostic documentation and operator runbooks. When operators run `journalctl -x`, the journal automatically renders this documentation alongside the event. Target Layout Extended harnesses this built-in Linux capability to formalize Lakos component boundaries.

**Dual channel decoupling.** The architecture separates application control flow from observability telemetry. Upstream C++ callers receive a clean boolean `false` or `std::nullopt`, preserving strict architectural decoupling. Concurrently, the runtime environment transmits structured diagnostic fields to `systemd-journald`.

**The catalog as a companion contract.** In Lakos physical design, a component is an atomic unit. In this repository, each operational directory contains `<name>.hpp` as the compile time contract and `<name>.catalog` as the runtime failure contract. Both live together under the physical one name rule. An engineer reads the header to see what the component does and reads the catalog to see how it can fail.

**In memory flight recorder with automatic lookbehind.** High volume debug traces remain inside a 2048 slot ring buffer with zero disk I/O. When an unhandled boundary exception occurs, the consumer thread automatically flushes recent skipped debug breadcrumbs ahead of the fault record.

**Pre unwinding binary stack capture.** Standard logging frameworks capture call stacks after unwinding reaches an outer catch block. They also frequently parse DWARF tables on the critical path. The journal infrastructure captures raw 64 bit instruction pointers directly into stack buffers at the throw site before unwinding begins. This enables zero overhead symbolization offline.

## Exception boundaries and when noexcept applies

Inside a component's `.cpp` file, standard C++ exceptions and throwing standard library or third party calls are often convenient. However, allowing exceptions to escape across subsystem boundaries introduces coupling between targets.

* **Hidden control flow.** A function signature in `<name>/<name>.hpp` does not declare which exception types its internal implementation or transitive `PRIVATE` dependencies might throw.
* **ABI and compiler flag friction.** Propagating exceptions across boundaries requires uniform unwinding tables and matching exception semantics across linked targets.
* **Unclear failure ownership.** When an uncaught exception unwinds across multiple components up the call stack, the context where the boundary contract was violated is harder to trace.

To keep subsystem interfaces narrow and predictable, public functions in operational components such as `database_manager`, `tbb_algos`, and `db_export` are marked `noexcept` and return explicit status values.

```cpp title="targets/database_manager/database_manager/database_manager.hpp"
[[nodiscard]] auto initialize(const std::filesystem::path& db_path) noexcept -> bool;
[[nodiscard]] auto add_item(const Item& item) noexcept -> bool;
```

### Is noexcept mandatory in a component design

Marking public functions `noexcept` is an architectural pattern for operational and subsystem boundaries, not a mandatory rule for every component.

* **Subsystem and service components.** Components that encapsulate I/O, thread pools, system calls, or third party libraries benefit from `noexcept` boundaries and Lippincott handlers because they act as firewalls between targets.
* **Data schemas and value types.** Pure data components such as `targets/item_proto/` expose generated Protobuf classes whose constructors and mutators allocate memory and follow standard C++ exception semantics. Wrapping fine grained value types or inline accessors in `noexcept` boundary loggers would add unnecessary overhead.

## The Lippincott function pattern

Writing repetitive `try` and `catch` blocks with multiple `catch` clauses inside every public method creates boilerplate clutter. Instead, **Target Layout Extended** uses a **Lippincott function** provided by `targets/journal/`.

A Lippincott function is a centralized exception handling routine called from a single `catch (...)` block at a `noexcept` boundary proxy. It rethrows the active exception internally via `std::rethrow_exception(std::current_exception())`, decodes the concrete exception type in one shared place, records the diagnostic event, and returns a clean error status.

Here is how `targets/database_manager/database_manager.cpp` guards its public boundary.

```cpp title="targets/database_manager/database_manager.cpp"
auto database_store::add_item(const Item& item) noexcept -> bool {
    try {
        if (item.id().empty()) {
            throw journal::catalog_exception(k_catalog.k_add_item_failed, "item id must not be empty");
        }
        std::scoped_lock const lock(_db_mutex);

        auto const items_view = std::ranges::subrange(
            _database.mutable_items()->begin(),
            _database.mutable_items()->end()
        );
        auto const it = std::ranges::find(items_view, item.id(), &Item::id);
        if (it != items_view.end()) {
            *it = item;
            return true;
        }

        *(_database.add_items()) = item;
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(k_catalog.k_add_item_failed);
    }
}
```

The failure originates inside the private implementation or precondition check. The public method acts as a boundary proxy that catches the exception and delegates to `journal::catch_boundary_exception()`.

### What the handler records for each exception type

`catch_boundary_exception()` always returns `false` and writes one journal event. The catalog entry and stack trace depend on what was thrown.

| Caught type | Catalog entry used | Priority | Stack trace origin |
| --- | --- | --- | --- |
| `journal::catalog_exception` | The entry carried by the exception | `error` | Throw site, captured before unwinding |
| `std::invalid_argument`, `std::out_of_range`, `std::system_error` | The default entry passed at the boundary | `error` | Boundary `catch` block |
| Any other `std::exception` | The default entry passed at the boundary | `error` | Boundary `catch` block |
| Anything else | The default entry passed at the boundary | `critical` | Boundary `catch` block |

Every boundary event carries `MESSAGE_ID`, `COMPONENT`, `PRIORITY`, `CODE_FILE`, `CODE_LINE` and `CODE_FUNC`. The `MESSAGE` field reads `[component] summary (type: what())`. The stack trace is stored in `RAW_PAYLOAD` as raw frame addresses, so symbolize it offline against the matching binary with a tool such as `addr2line`.

`catalog_exception` is not derived from `std::exception`. A generic `catch (const std::exception&)` inside a helper will therefore not swallow it on its way to the boundary.

## Cataloged boundary logs versus internal diagnostics

The `targets/journal/` component provides two separate observability mechanisms.

### Cataloged boundary contract failures

When an exception forces a public boundary function to abort, `journal::catch_boundary_exception()` logs a structured event tied to a permanent 32 character hexadecimal message identifier.

Each component declares its failure contracts in a closed `struct catalog` aggregate inside its public header.

```cpp title="targets/database_manager/database_manager/database_manager.hpp"
struct catalog {
    journal::catalog_entry k_init_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e101",
        .component = "database_manager",
        .summary = "Database initialization failure caught at component boundary"
    };
    journal::catalog_entry k_add_item_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e102",
        .component = "database_manager",
        .summary = "Item insertion failure caught at component boundary"
    };
    // k_save_failed and k_load_failed follow the same pattern
};
inline constexpr catalog k_catalog{};
```

### Automatic deduction and explicit selection

Components select catalog entries according to the size of their catalog.

* **Single entry deduction.** When `struct catalog` contains exactly one entry, the `journal::single_entry_catalog` concept is satisfied. The boundary handler calls `journal::catch_boundary_exception(k_catalog)` without naming the entry, as `tbb_algos` does.
* **Multiple entry selection.** When `struct catalog` contains more than one entry, that overload is removed from the candidate set. The boundary handler must call `journal::catch_boundary_exception(k_catalog.k_add_item_failed)` to name its fallback contract.
* **Pre unwinding stack trace capture.** Internal functions can throw `journal::catalog_exception` to select a more specific entry. The constructor records up to 32 frames at the throw site before unwinding destroys them.

### Rules the catalog must follow

`journal.hpp` counts entries by comparing `sizeof(catalog)` with `sizeof(catalog_entry)` and validates them with structured bindings in a `consteval` function. That imposes a few constraints.

* **Only `catalog_entry` members.** Any other member breaks the size based count and the catalog is rejected.
* **At most eight entries.** `is_valid_catalog()` unpacks up to eight bindings. A component that needs more failure contracts is usually doing too much and should be split.
* **Valid identifiers.** Each `message_id` must be 32 lowercase hexadecimal characters. Generate one with `journalctl --new-id128` or `systemd-id128 new` and copy the plain form.
* **Mirror the catalog file.** Every entry needs a matching `-- <message_id>` block in `<name>.catalog`. Nothing enforces this at build time yet, so treat it as part of code review.

### Internal operational logging

Not every diagnostic event represents a boundary failure. For internal operational logs, `targets/journal/` provides `journal::log` with strongly typed arguments. It deduces `COMPONENT` from the first path segment after `targets/` in `std::source_location::file_name()`.

```cpp title="Operational text and binary logging"
journal::log(
    journal::severity::error,
    journal::message{"parent directory does not exist for path: " + path.string()}
);

journal::log(
    journal::as_binary_payload(packet_header),
    journal::message{"corrupted packet header snapshot"},
    journal::severity::debug
);
```

The `valid_log_pack` concept checks each call at compile time. Arguments may come in any order. There must be exactly one `journal::severity` and at least one of `journal::message` or `journal::binary_payload`. `as_binary_payload()` accepts any trivially copyable value or a `std::span` of them and stores the bytes in `RAW_PAYLOAD` without hex encoding. Internal events never carry a `MESSAGE_ID`, which keeps `<name>.catalog` reserved for boundary contracts.

## Binary stack traces and offline symbolization

The binary logging architecture in `targets/journal/` separates diagnostic recording from diagnostic symbolization.

Traditional logging frameworks parse DWARF debug sections and demangle C++ identifiers during failure handling. That approach causes substantial runtime overhead, allocates heap strings, and risks memory corruption inside failing processes. In contrast, `targets/journal/` captures raw 64 bit instruction pointers directly into stack buffers.

### Advantages of binary stack traces

**Zero symbolization overhead.** The process never inspects symbol tables or DWARF sections while handling errors. No string allocations take place on the failure path.

**Pre unwinding frame capture.** When an internal helper throws `journal::catalog_exception`, the constructor captures the active stack frames before stack unwinding begins. The original failure context remains intact.

**Compact binary footprint.** Every stack frame takes exactly 8 bytes of storage. A 16 frame call stack fits inside 128 bytes of binary payload.

**Native systemd integration.** The binary payload transmits via `sd_journal_sendv` into `systemd-journald` under the field `RAW_PAYLOAD`.

### Symbolizing journal entries with addr2line

Operators and continuous integration runners can retrieve the binary log entry using `journalctl`.

```bash title="Query journal entry in JSON format"
journalctl -g "snapshot \+ trace" -n 1 -o json
```

In the JSON output, `systemd-journald` represents `RAW_PAYLOAD` as a numeric byte array.

```json title="systemd-journald JSON record with RAW_PAYLOAD"
{
  "MESSAGE": "[journal] snapshot + trace",
  "CODE_LINE": "232",
  "RAW_PAYLOAD": [191,88,64,0,0,0,0,0, 214,54,64,0,0,0,0,0]
}
```

Every 8 bytes form one little endian 64 bit address. Unpacking the first two entries produces addresses `0x4058bf` and `0x4036d6`. Passing them to `addr2line` resolves the exact C++ source locations.

```bash title="Resolve frame addresses with addr2line"
addr2line -e ./Build/Clang_Custom_Debug/targets/journal/journalTEST/journalTEST -f -C 0x4058bf 0x4036d6
```

The symbolizer prints the demangled function signature along with the file and line number.

```text title="Demangled source locations"
target_layout_extended::journal::detail::capture_current_stacktrace(std::span<unsigned long, 18446744073709551615ul>)
targets/journal/journal.cpp:218
JournalTest_EmitsTextAndBinaryLogsAcrossAllSyslogSeveritiesInAnyArgumentOrder_Test::TestBody()
targets/journal/journalTEST/journalTEST.cpp:214
```

### Automated showcase in unit tests

The unit test suite in `targets/journal/journalTEST/journalTEST.cpp` contains an automated verification test named `SymbolizesBinaryStacktraceAddressesUsingAddr2line`.

The test throws a `catalog_exception` and extracts the raw binary payload. It computes executable base offsets to support position independent code, and executes `addr2line` via a pipe. It verifies that the demangled output matches the test body and source filename.

## The catalog file as component documentation

In standard `systemd-journald`, a message catalog file maps 128 bit `MESSAGE_ID` UUIDs to human readable explanations and support links.

In **Target Layout Extended**, each operational component stores its `<name>.catalog` file inside `targets/<name>/<name>.catalog`.

```text title="targets/database_manager/database_manager.catalog"
# systemd journald message catalog for component: database_manager

-- 4c8a1023e5b94d719f012a34c8b5e101
Subject: Database initialization failure caught at component boundary
Defined-By: target_layout_extended::database_manager
Support: https://github.com/JeffKarling/Target_Layout_Extended

The database_store::initialize boundary proxy caught an exception from internal
initialization because the supplied path was empty or an existing database file
could not be parsed.
```

Colocating `<name>.catalog` inside the component folder keeps logging aligned with the code.

* **Clear boundary specification.** A developer or AI agent inspecting `targets/database_manager/` reads `database_manager/database_manager.hpp` to see what functions can be called, and reads `database_manager.catalog` to see which internal failures the component boundary catches and reports.
* **Direct production triage.** An operator who sees `COMPONENT=database_manager` or a known `MESSAGE_ID` in the journal knows exactly which folder owns the failure and which header declares it.
* **Unit tested catalog hygiene.** The component's unit test in `<name>TEST/` verifies that declared `MESSAGE_ID` strings conform to the 32 character lowercase hexadecimal UUID rule and that invalid boundary inputs trigger clean `false` returns without leaking exceptions.

## Querying and installing catalogs

Boundary and internal events can be filtered by any structured field.

```bash title="Filter structured journal events by component and message ID"
journalctl COMPONENT=database_manager
journalctl MESSAGE_ID=4c8a1023e5b94d719f012a34c8b5e101
journalctl COMPONENT=database_manager PRIORITY=3 -o verbose
```

The build does not install `<name>.catalog` files. The explanation text only appears in `journalctl -x` once the files are registered with the local catalog database.

```bash title="Install and update systemd message catalogs"
sudo cp targets/*/*.catalog /usr/lib/systemd/catalog/
sudo journalctl --update-catalog
journalctl -x MESSAGE_ID=4c8a1023e5b94d719f012a34c8b5e101
```

Packaging the catalogs as part of an `install()` rule is left to the consuming project.
