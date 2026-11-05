# Journal developer guide

This guide explains how to use `journal` when building a new component under Target Layout Extended. Developers accustomed to libraries like `spdlog` will notice key differences in mental model, design rules and performance characteristics.

## Mental model transition from spdlog

In `spdlog`, logging interpolates runtime variables into a formatted string on the calling thread before writing to console or file sinks.

```cpp title="Traditional spdlog runtime string formatting on caller thread"
// spdlog formats item_id and flags into a text buffer on the calling thread
spdlog::info("Processing item {} with flags {:x}", item_id, flags);
```

In `journal`, `journal::log` intentionally does **not** accept format strings or runtime text interpolation arguments. Converting runtime integers, floats and structs into formatted strings on the producer thread costs hundreds of nanoseconds and risks heap allocations. Instead, `journal` separates the static event description from the runtime variable state.

```cpp title="Equivalent zero-formatting journal call with runtime variables"
struct item_context {
    std::uint64_t item_id;
    std::uint32_t flags;
};

// Static message string paired with raw binary runtime variables
jrn::log(
    jrn::severity::info,
    jrn::message{"Processing item"},
    jrn::as_binary_payload(item_context{.item_id = item_id, .flags = flags})
);
```

This design follows three core principles.

1. **Zero string formatting on the producer thread.** `journal::message` carries a static description while runtime variables are passed as raw bytes via `journal::as_binary_payload`. The producer copies the raw bytes and hardware cycle timestamp into the lock free ring buffer in roughly 30 nanoseconds without running `std::format` or `snprintf`.
2. **Native systemd-journald integration.** Logs become structured systemd journal fields. Standard attributes include `MESSAGE`, `PRIORITY` and `COMPONENT`. Diagnostics attach `CODE_FILE`, `CODE_LINE`, `RAW_PAYLOAD`[^1] and `MESSAGE_ID`.
3. **Compiler enforced component taxonomy.** Components do not manually declare logger names. The component name is deduced automatically from directory layout at compile time under the physical one name rule.

## Adding journal to CMakeLists.txt

In your component `targets/<name>/CMakeLists.txt`, link `journal` as a private or public dependency.

```cmake title="targets/<name>/CMakeLists.txt"
target_link_libraries(${DIR_NAME}
    PUBLIC
        ${PROJECT_NAME}::journal
)
```

Include the public header in your code using angle brackets.

```cpp title="Include journal public header"
#include <journal/journal.hpp>
```

## Basic diagnostic logging

Diagnostic logs use `journal::log` with strongly typed attributes. Attributes can be passed in any order.

```cpp title="Order independent diagnostic logging"
namespace jrn = tle::journal;

// Log static plain text
jrn::log(jrn::severity::info, jrn::message{"Worker initialized"});

// Arguments are order-independent
jrn::log(jrn::message{"Cache lookup succeeded"}, jrn::severity::debug);

// Attach a single runtime variable or struct without string formatting
jrn::log(jrn::severity::debug, jrn::message{"Batch size"}, jrn::as_binary_payload(batch_count));
```

!!! warning "Avoid constructing runtime std::string messages on hot paths"
    Because `journal::message` wraps a `std::string_view` and copies up to 512 bytes into the ring slot during `journal::log`, cold error paths can pass a temporary string expression when needed. Never concatenate or format `std::string` objects on hot paths, as heap allocation and text formatting on the caller thread defeat the lock free ring buffer. Pass runtime variables through `journal::as_binary_payload` instead.

### Supported severity levels

`systemd-journald` uses the standard RFC 5424 and POSIX Syslog numerical priority scheme ranging from `0` for emergency to `7` for debug. In the journald wire protocol, this is represented by the metadata field `PRIORITY=<digit>`.

`journal::severity` is a strongly typed C++ scoped enum whose underlying numerical values `0U` through `7U` map directly to the POSIX `<syslog.h>` constants `LOG_EMERG` through `LOG_DEBUG` and are validated at compile time with static assertions.

```cpp title="targets/journal/journal/journal.hpp"
enum class severity : std::uint8_t {
    emergency = 0U, // LOG_EMERG   (PRIORITY=0)
    alert     = 1U, // LOG_ALERT   (PRIORITY=1)
    critical  = 2U, // LOG_CRIT    (PRIORITY=2)
    error     = 3U, // LOG_ERR     (PRIORITY=3)
    warning   = 4U, // LOG_WARNING (PRIORITY=4)
    notice    = 5U, // LOG_NOTICE  (PRIORITY=5)
    info      = 6U, // LOG_INFO    (PRIORITY=6)
    debug     = 7U  // LOG_DEBUG   (PRIORITY=7)
};
```

When journal entries are sent to journald, the consumer thread writes `PRIORITY=<digit>`, allowing tools like `journalctl -p err` or `journalctl -p 3` to filter records natively.

### Logging binary payloads

Unlike `spdlog` where binary data must be converted to hex strings, `journal` accepts raw binary payloads natively without string conversions.

```cpp title="Logging trivially copyable structs and byte spans"
struct packet_header {
    std::uint32_t magic;
    std::uint32_t length;
};

packet_header header{.magic = 0xAABBCCDD, .length = 128};

// Pass struct directly as binary payload
jrn::log(
    jrn::severity::info,
    jrn::message{"Received raw packet header"},
    jrn::as_binary_payload(header)
);

// Or pass a raw span of std::byte
std::span<const std::byte> raw_bytes = get_raw_buffer();
jrn::log(jrn::severity::debug, jrn::binary_payload{raw_bytes});
```

Binary payloads are sent to systemd-journald under the `RAW_PAYLOAD=` field.[^1]

## Catalog entries and boundary exception firewall

Target Layout Extended requires clean component boundaries. Exceptions must never leak across public component boundaries.

Instead of writing custom catch blocks, every component defines a `.catalog` file and a `struct catalog` in its header containing 32 character hexadecimal UUIDs.

### Step 1. Define catalog entry in header

Declare the catalog struct in `targets/<name>/<name>/<name>.hpp`.

```cpp title="targets/<name>/<name>/<name>.hpp"
namespace tle::<name> {

    struct catalog {
        journal::catalog_entry k_boundary_fault{
            .message_id = "51a3b901c2d44f6e87a213098df24103",
            .component = "<name>",
            .summary = "Unhandled fault caught at component boundary firewall"
        };
    };
    inline constexpr catalog k_catalog{};

} // namespace tle::<name>
```

### Step 2. Document catalog entry in catalog file

Add the message catalog entry in `targets/<name>/<name>.catalog`.

```text title="targets/<name>/<name>.catalog"
-- 51a3b901c2d44f6e87a213098df24103
Subject: Component boundary failure in <name>
Defined-By: <name>

An unhandled exception escaped internal logic and was caught by the boundary firewall.
```

### Step 3. Protect public functions with boundary firewall

Wrap the public function in `targets/<name>/<name>.cpp` with a single try-catch block.

```cpp title="targets/<name>/<name>.cpp"
auto my_component::execute_task() noexcept -> bool {
    try {
        // Internal logic here
        jrn::log(jrn::severity::info, jrn::message{"Task completed"});
        return true;
    } catch (...) {
        return jrn::catch_boundary_exception(k_catalog);
    }
}
```

When an exception occurs, `catch_boundary_exception` performs five actions automatically.

* **Exception message extraction.** Captures the exception type and error text from `std::exception::what()`.
* **Binary stack trace capture.** Captures a binary stack trace of return addresses without string allocation.
* **Structured catalog emission.** Logs a structured error to the journal with `MESSAGE_ID=51a3b901c2d44f6e87a213098df24103`.
* **Lookbehind history flush.** Flushes recent suppressed debug breadcrumbs ahead of the error.
* **Clean error return.** Returns `false`, preventing the exception from escaping across the component boundary.

### Step 4. Internal catalog exceptions

Internal functions can throw `journal::catalog_exception` to pinpoint exact failure modes.

```cpp title="Throwing catalog_exception with pre-unwind stack trace"
void internal_step(int step_id) {
    if (step_id < 0) {
        throw jrn::catalog_exception(
            k_catalog.k_boundary_fault,
            "Invalid negative step id provided"
        );
    }
}
```

The stack trace is frozen at the throw site before stack unwinding begins.

## Flight recorder and runtime policies

By default, `journal` operates in flight recorder mode.

* **Default flight recorder behavior.** High volume `debug` records stay in the 2048 slot ring buffer with zero `systemd-journald` I/O traffic. When a record at or above `lookbehind_trigger` occurs, recent breadcrumbs flush automatically with `LOG_LOOKBEHIND=1` ahead of the fault record.
* **Public policy control surface.** The public header exposes `journal::policy` and `journal::component_override` alongside five control functions. Use `journal::is_valid_policy` and `journal::activate_policy` to validate and apply candidate configurations. Call `journal::active_policy` and `journal::statistics_snapshot` to inspect state. Call `journal::reset_default_policy` to restore flight recorder defaults.
* **External adapter capability.** The core logging engine uses a protocol neutral last caller wins contract. External control belongs in higher level adapter components. Applications can use `targets/journal_signal_adapter` for POSIX signals or dedicated services for Varlink, D-Bus, and HTTP REST.
* **Technical reference link.** See [journal_policy_example.md](journal_policy_example.md) for full struct layouts and adapter component examples.

```cpp title="Activating a per-component debug microscope policy"
jrn::policy policy{};
// Stream every debug log from the geometry component
auto override_entry = jrn::make_component_override("geometry", 1U);
if (override_entry.has_value()) {
    policy.overrides.at(0U) = *override_entry;
    jrn::activate_policy(policy);
}
```

## Unit testing with custom sinks

In unit tests, redirect journal emissions into memory using `journal::detail::set_record_sink`.

```cpp title="targets/<name>/<name>TEST/<name>TEST.cpp"
#include <journal/journal.hpp>
#include <gtest/gtest.h>

TEST(MyComponentTest, VerifiesLogOutput) {
    std::vector<std::string> messages;

    auto callback = [](void* context, const jrn::detail::emitted_record& record) noexcept {
        auto* list = static_cast<std::vector<std::string>*>(context);
        list->push_back(std::string(record.text));
    };

    jrn::detail::set_record_sink({callback, &messages});

    // Run component logic
    my_component comp;
    comp.do_work();

    // Flush consumer thread
    jrn::flush(std::chrono::milliseconds{100});

    EXPECT_FALSE(messages.empty());

    jrn::detail::reset_record_sink();
}
```

[^1]: Because `RAW_PAYLOAD` contains non-printable bytes, `journalctl` hides it in default text output and serializes it as a JSON array of unsigned byte integers when queried with `-o json`. To inspect the raw byte array with `jq`, run `journalctl -n 1 -o json | jq '.RAW_PAYLOAD'`. To decode a logged C++ struct such as `item_context` with `uint64_t` and `uint32_t` fields back into typed values on the command line, unpack the JSON byte array with Python's `struct` module via `journalctl -n 1 -o json | python3 -c "import sys, json, struct; print(struct.unpack('<QI', bytes(json.load(sys.stdin)['RAW_PAYLOAD'])[:12]))"`. For boundary failure stack traces stored in `RAW_PAYLOAD`, see the `addr2line` symbolization workflow in [component boundaries and catalogs](component_boundaries.md).
