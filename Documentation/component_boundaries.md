# Component boundaries and catalogs

In a system built around physical components, the boundary between components is an architectural wall. A component consumer only sees `<name>/<name>.hpp` and links `${PROJECT_NAME}::<name>`. Everything else stays private inside `targets/<name>/`.

**Target Layout Extended** enforces two runtime disciplines at every component wall. First, exceptions never cross a component boundary. Second, every boundary failure is logged to `systemd-journald` using a documented catalog identifier stored inside the component directory.

## 1. Why exceptions must not cross component walls

Inside a component's `.cpp` file, standard C++ exceptions and throwing standard library or third party calls are often convenient. However, allowing exceptions to escape across physical component boundaries introduces severe coupling.

* **Hidden control flow.** A function signature in `<name>/<name>.hpp` does not declare which exception types its internal implementation or transitive `PRIVATE` dependencies might throw.
* **ABI and compiler flag friction.** Propagating exceptions across boundaries requires uniform unwinding tables and matching exception semantics across all linked targets.
* **Unclear failure ownership.** When an uncaught exception unwinds five components up the call stack, the stack context where the boundary contract actually failed is lost.

To keep component interfaces narrow and deterministic, public functions in `<name>/<name>.hpp` are marked `noexcept` and return explicit status values.

```cpp
[[nodiscard]] auto initialize(const std::filesystem::path& db_path) noexcept -> bool;
[[nodiscard]] auto add_item(const Item& item) noexcept -> bool;
```

## 2. The Lippincott function pattern

Writing repetitive `try` and `catch` blocks with five different `catch` clauses inside every public method creates boilerplate clutter. Instead, **Target Layout Extended** uses a **Lippincott function** provided by `targets/journal/`.

A Lippincott function is a centralized exception handling routine called from a single `catch (...)` block at a `noexcept` boundary. It rethrows the active exception internally (`std::rethrow_exception(std::current_exception())`), decodes the concrete exception type in one shared place, records the diagnostic event, and returns a clean error status.

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

Every public entry point in the component needs only a three line `catch (...)` block that delegates to `journal::catch_boundary_exception()`.

## 3. Structured `systemd-journald` logging and catalog IDs

When `journal::catch_boundary_exception()` intercepts an exception, it does not print unstructured text to `stderr`. Instead, it emits a structured event to Linux `systemd-journald` via `sd_journal_send()` containing exact metadata fields.

* **`MESSAGE_ID`.** A 128 bit lowercase hexadecimal UUID uniquely identifying the public boundary condition.
* **`COMPONENT`.** The physical component name matching `${DIR_NAME}`.
* **`CODE_FILE`, `CODE_LINE`, and `CODE_FUNC`.** Captured automatically at the component boundary via C++20 `std::source_location::current()`.
* **`PRIORITY`.** Syslog severity level (`LOG_ERR` or `LOG_CRIT`).

Each component declares its public catalog entries right in its public header (`database_manager/database_manager.hpp`).

```cpp
namespace catalog {
    inline constexpr journal::catalog_entry k_init_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e101",
        .component = "database_manager",
        .summary = "Database initialization failed at component boundary"
    };
    inline constexpr journal::catalog_entry k_add_item_failed{
        .message_id = "4c8a1023e5b94d719f012a34c8b5e102",
        .component = "database_manager",
        .summary = "Item insertion rejected at component boundary"
    };
} // namespace catalog
```

## 4. The `.catalog` file as executable component documentation

In standard `systemd-journald`, a message catalog file maps 128 bit `MESSAGE_ID` UUIDs to human readable explanations, root causes, and support links.

In **Target Layout Extended**, every component owns its `<name>.catalog` file right inside `targets/<name>/<name>.catalog`.

```text
# systemd journald message catalog for component: database_manager

-- 4c8a1023e5b94d719f012a34c8b5e101
Subject: Database initialization failed at component boundary
Defined-By: target_layout_extended::database_manager
Support: https://github.com/JeffKarling/Target_Layout_Extended

The database_store::initialize boundary call failed because the supplied path
was empty or an existing database file could not be parsed.
```

Colocating `<name>.catalog` inside the component folder turns logging into **living architectural documentation**.

* **Complete boundary specification.** A developer or AI agent inspecting `targets/database_manager/` reads `database_manager/database_manager.hpp` to see what functions can be called, and reads `database_manager.catalog` to see every failure mode that component can report at runtime.
* **Zero search production triage.** When an operator or test runner queries `journalctl COMPONENT=database_manager` or filters by `MESSAGE_ID=4c8a1023e5b94d719f012a34c8b5e101`, the exact component folder and boundary explanation are immediately known.
* **Unit tested catalog hygiene.** The component's unit test in `<name>TEST/` verifies that all declared `MESSAGE_ID` strings conform to the 32 character lowercase hexadecimal UUID rule and that invalid boundary inputs trigger clean `false` returns without leaking exceptions.
