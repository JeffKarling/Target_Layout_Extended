# Component boundaries and catalogs

In a system built around physical components, the boundary between components acts as an architectural wall. A component consumer only sees `<name>/<name>.hpp` and links `${PROJECT_NAME}::<name>`. Everything else stays private inside `targets/<name>/`.

**Target Layout Extended** demonstrates two runtime practices at service and subsystem walls. First, exceptions thrown inside private internals are caught at the component boundary proxy before crossing into another CMake target. Second, boundary failures and internal diagnostics are recorded to `systemd-journald` through `targets/journal/`.

## Exception boundaries and when noexcept applies

Inside a component's `.cpp` file, standard C++ exceptions and throwing standard library or third party calls are often convenient. However, allowing exceptions to escape across subsystem boundaries introduces coupling between targets.

* **Hidden control flow.** A function signature in `<name>/<name>.hpp` does not declare which exception types its internal implementation or transitive `PRIVATE` dependencies might throw.
* **ABI and compiler flag friction.** Propagating exceptions across boundaries requires uniform unwinding tables and matching exception semantics across linked targets.
* **Unclear failure ownership.** When an uncaught exception unwinds across multiple components up the call stack, the context where the boundary contract was violated is harder to trace.

To keep subsystem interfaces narrow and predictable, public functions in operational components such as `database_manager`, `tbb_algos`, and `db_export` are marked `noexcept` and return explicit status values.

```cpp
[[nodiscard]] auto initialize(const std::filesystem::path& db_path) noexcept -> bool;
[[nodiscard]] auto add_item(const Item& item) noexcept -> bool;
```

### Is noexcept mandatory in a component design

Marking public functions `noexcept` is an architectural pattern for operational and subsystem boundaries, not a mandatory rule for every component.

* **Subsystem and service components.** Components that encapsulate I/O, thread pools, system calls, or third party libraries benefit from `noexcept` boundaries and Lippincott handlers because they act as firewalls between targets.
* **Data schemas and value types.** Pure data components such as `targets/item_schema/` expose generated Protobuf classes whose constructors and mutators allocate memory and follow standard C++ exception semantics. Wrapping fine grained value types or inline accessors in `noexcept` boundary loggers would add unnecessary overhead.

## The Lippincott function pattern

Writing repetitive `try` and `catch` blocks with multiple `catch` clauses inside every public method creates boilerplate clutter. Instead, **Target Layout Extended** uses a **Lippincott function** provided by `targets/journal/`.

A Lippincott function is a centralized exception handling routine called from a single `catch (...)` block at a `noexcept` boundary proxy. It rethrows the active exception internally via `std::rethrow_exception(std::current_exception())`, decodes the concrete exception type in one shared place, records the diagnostic event, and returns a clean error status.

Here is how `targets/database_manager/database_manager.cpp` guards its public boundary.

```cpp
auto database_store::add_item(const Item& item) noexcept -> bool {
    try {
        if (item.id().empty()) {
            throw std::invalid_argument("item id must not be empty");
        }
        std::lock_guard<std::mutex> const lock(_db_mutex);

        for (int i = 0; i < _database.items_size(); ++i) {
            if (_database.items(i).id() == item.id()) {
                *(_database.mutable_items(i)) = item;
                return true;
            }
        }

        *(_database.add_items()) = item;
        return true;
    } catch (...) {
        return journal::catch_boundary_exception(catalog::k_add_item_failed);
    }
}
```

The failure originates inside the private implementation or precondition check, while the public method acts as a boundary proxy that catches the exception and delegates to `journal::catch_boundary_exception()`.

## Cataloged boundary logs versus non cataloged internal logs

The `targets/journal/` component supports two distinct logging modes over `sd_journal_send()`. Both modes attach structured `COMPONENT`, `PRIORITY`, `CODE_FILE`, `CODE_LINE`, and `CODE_FUNC` fields captured via C++20 `std::source_location::current()`.

### Cataloged boundary logs

When an exception is caught at a component's public boundary proxy, `journal::catch_boundary_exception()` or `journal::log_error(entry, detail)` emits a structured event tied to a 128 bit hexadecimal `MESSAGE_ID`.

Each operational component declares its public catalog entries in its public header, as seen in `database_manager/database_manager.hpp`.

```cpp
namespace catalog {
    inline constexpr journal::catalog_entry k_init_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e101",
        .component = "database_manager",
        .summary = "Database initialization failure caught at component boundary"
    };
    inline constexpr journal::catalog_entry k_add_item_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e102",
        .component = "database_manager",
        .summary = "Item insertion failure caught at component boundary"
    };
} // namespace catalog
```

### Non cataloged internal logs

Not every internal error or diagnostic event warrants a permanent 128 bit UUID in `<name>.catalog`. For internal helper functions and operational diagnostics, `targets/journal/` provides non cataloged `journal::log_error(component, message)` and `journal::log_info(component, message)` overloads.

For example, `targets/database_manager/database_helper.cpp` logs a non cataloged error when an internal path check sees a missing parent directory.

```cpp
std::error_code ec;
if (!std::filesystem::exists(parent, ec)) {
    journal::log_error("database_manager", "parent directory does not exist for path: " + path.string());
    return false;
}
```

This event is still tagged with `COMPONENT=database_manager` and exact source file and line metadata in `systemd-journald`, keeping `<name>.catalog` reserved for public boundary contracts.

## The catalog file as component documentation

In standard `systemd-journald`, a message catalog file maps 128 bit `MESSAGE_ID` UUIDs to human readable explanations and support links.

In **Target Layout Extended**, each operational component stores its `<name>.catalog` file inside `targets/<name>/<name>.catalog`.

```text
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
* **Direct production triage.** When an operator or test runner queries `journalctl COMPONENT=database_manager` or filters by `MESSAGE_ID=4c8a1023e5b94d719f012a34c8b5e101`, the component folder and boundary explanation are immediately available.
* **Unit tested catalog hygiene.** The component's unit test in `<name>TEST/` verifies that declared `MESSAGE_ID` strings conform to the 32 character lowercase hexadecimal UUID rule and that invalid boundary inputs trigger clean `false` returns without leaking exceptions.
