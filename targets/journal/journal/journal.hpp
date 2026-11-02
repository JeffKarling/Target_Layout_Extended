#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace target_layout_extended::journal {

    // The 8 standard RFC 5424 and Syslog numerical severity classifications.
    enum class severity : std::uint8_t {
        emergency = 0U,
        alert     = 1U,
        critical  = 2U,
        error     = 3U,
        warning   = 4U,
        notice    = 5U,
        info      = 6U,
        debug     = 7U
    };

    inline constexpr std::size_t k_severity_count = 8U;

    [[nodiscard]] constexpr auto to_syslog_priority(severity level) noexcept -> int {
        return static_cast<int>(level);
    }

    [[nodiscard]] constexpr auto is_valid_severity(severity level) noexcept -> bool {
        return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(severity::debug);
    }

    inline constexpr std::size_t k_uuid_hex_length = 32U;
    inline constexpr std::size_t k_max_stacktrace_frames = 32U;
    inline constexpr std::size_t k_max_exception_reason_length = 255U;

    inline constexpr std::size_t k_catalog_arity_1 = 1U;
    inline constexpr std::size_t k_catalog_arity_2 = 2U;
    inline constexpr std::size_t k_catalog_arity_3 = 3U;
    inline constexpr std::size_t k_catalog_arity_4 = 4U;
    inline constexpr std::size_t k_catalog_arity_5 = 5U;
    inline constexpr std::size_t k_catalog_arity_6 = 6U;
    inline constexpr std::size_t k_catalog_arity_7 = 7U;
    inline constexpr std::size_t k_catalog_arity_8 = 8U;

    // Validates that a catalog MESSAGE_ID is a 32-character lowercase hexadecimal UUID string.
    [[nodiscard]] constexpr auto is_valid_message_id(std::string_view message_id) noexcept -> bool {
        if (message_id.size() != k_uuid_hex_length) {
            return false;
        }
        return std::ranges::all_of(message_id, [](char const character) noexcept {
            bool const is_digit = (character >= '0' && character <= '9');
            bool const is_hex_lower = (character >= 'a' && character <= 'f');
            return is_digit || is_hex_lower;
        });
    }

    // Represents a documented systemd-journald catalog entry strictly for a component boundary failure contract.
    // Every public MESSAGE_ID corresponds to an entry in the component's <name>.catalog file.
    struct catalog_entry {
        std::string_view message_id;
        std::string_view component;
        std::string_view summary;

        [[nodiscard]] constexpr auto is_valid() const noexcept -> bool {
            return is_valid_message_id(message_id) && !component.empty() && !summary.empty();
        }
    };

    // Closed catalog definition for the journal component itself.
    struct catalog {
        catalog_entry k_unhandled_boundary_exception{
            .message_id = "7a1d4f90e3c84a21b5809e41d2a7c001",
            .component = "journal",
            .summary = "Unhandled exception caught at component boundary"
        };
    };
    inline constexpr catalog k_catalog{};

    // Computes the number of catalog_entry fields inside a component's closed struct catalog aggregate.
    template <typename Catalog>
    inline constexpr std::size_t catalog_entry_count_v =
        std::is_empty_v<std::remove_cvref_t<Catalog>>
            ? 0U
            : (sizeof(std::remove_cvref_t<Catalog>) / sizeof(catalog_entry));

    // Validates at compile time that every entry in a component's struct catalog has a valid UUID and metadata.
    template <typename Catalog>
    [[nodiscard]] consteval auto is_valid_catalog(const Catalog& component_catalog = Catalog{}) noexcept -> bool {
        if constexpr (!std::is_aggregate_v<std::remove_cvref_t<Catalog>>) {
            return false;
        } else {
            constexpr std::size_t count = catalog_entry_count_v<Catalog>;
            if constexpr (
                count == 0U ||
                count > k_catalog_arity_8 ||
                (sizeof(std::remove_cvref_t<Catalog>) != count * sizeof(catalog_entry))
            ) {
                return false;
            } else if constexpr (count == k_catalog_arity_1) {
                auto const& [e1] = component_catalog;
                return e1.is_valid();
            } else if constexpr (count == k_catalog_arity_2) {
                auto const& [e1, e2] = component_catalog;
                return e1.is_valid() && e2.is_valid();
            } else if constexpr (count == k_catalog_arity_3) {
                auto const& [e1, e2, e3] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid();
            } else if constexpr (count == k_catalog_arity_4) {
                auto const& [e1, e2, e3, e4] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid() && e4.is_valid();
            } else if constexpr (count == k_catalog_arity_5) {
                auto const& [e1, e2, e3, e4, e5] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid() && e4.is_valid() && e5.is_valid();
            } else if constexpr (count == k_catalog_arity_6) {
                auto const& [e1, e2, e3, e4, e5, e6] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid() && e4.is_valid() && e5.is_valid() && e6.is_valid();
            } else if constexpr (count == k_catalog_arity_7) {
                auto const& [e1, e2, e3, e4, e5, e6, e7] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid() && e4.is_valid() && e5.is_valid() && e6.is_valid() && e7.is_valid();
            } else {
                auto const& [e1, e2, e3, e4, e5, e6, e7, e8] = component_catalog;
                return e1.is_valid() && e2.is_valid() && e3.is_valid() && e4.is_valid() && e5.is_valid() && e6.is_valid() && e7.is_valid() && e8.is_valid();
            }
        }
    }

    // Concept satisfied only when a component defines a closed catalog struct containing exactly one valid catalog_entry.
    template <typename Catalog>
    concept single_entry_catalog =
        std::is_aggregate_v<std::remove_cvref_t<Catalog>> &&
        !std::same_as<std::remove_cvref_t<Catalog>, catalog_entry> &&
        (sizeof(std::remove_cvref_t<Catalog>) == sizeof(catalog_entry)) &&
        (catalog_entry_count_v<Catalog> == 1U) &&
        is_valid_catalog(std::remove_cvref_t<Catalog>{});

    template <single_entry_catalog Catalog>
    [[nodiscard]] consteval auto extract_single_catalog_entry(
        const Catalog& component_catalog = Catalog{}
    ) noexcept -> catalog_entry {
        auto const [only_entry] = component_catalog;
        return only_entry;
    }

    // Extracts the component name from a file path under the physical one name rule.
    // Supports custom root directory markers, Windows backslashes, legacy prefixes, and safe fallbacks.
    [[nodiscard]] constexpr auto extract_component(
        std::string_view file_path,
        std::string_view marker = "targets/"
    ) noexcept -> std::string_view {
        if (file_path.empty()) {
            return "unknown";
        }
        auto pos = file_path.rfind(marker);
        auto marker_len = marker.size();
        if (pos == std::string_view::npos && marker == "targets/") {
            constexpr std::string_view win_marker = "targets\\";
            pos = file_path.rfind(win_marker);
            if (pos != std::string_view::npos) {
                marker_len = win_marker.size();
            } else {
                constexpr std::string_view legacy = "srcTargets/";
                pos = file_path.rfind(legacy);
                if (pos != std::string_view::npos) {
                    marker_len = legacy.size();
                } else {
                    constexpr std::string_view legacy_win = "srcTargets\\";
                    pos = file_path.rfind(legacy_win);
                    if (pos != std::string_view::npos) {
                        marker_len = legacy_win.size();
                    }
                }
            }
        }
        if (pos == std::string_view::npos) {
            return "unknown";
        }

        auto start = pos + marker_len;
        while (start < file_path.size() && (file_path.at(start) == '/' || file_path.at(start) == '\\')) {
            ++start;
        }
        if (start >= file_path.size()) {
            return "unknown";
        }

        auto const next_sep = file_path.find_first_of("/\\", start);
        if (next_sep == std::string_view::npos) {
            auto const dot = file_path.rfind('.');
            if (dot != std::string_view::npos && dot > start) {
                return file_path.substr(start, dot - start);
            }
            return file_path.substr(start);
        }
        auto const result = file_path.substr(start, next_sep - start);
        return result.empty() ? "unknown" : result;
    }

    // Capacity of the preallocated flight recorder ring in records. Must stay a power of two.
    inline constexpr std::size_t k_ring_capacity = 2048U;
    static_assert(std::has_single_bit(k_ring_capacity));

    // Longest text and binary payload stored per record. Longer payloads are truncated on the producer side.
    inline constexpr std::size_t k_max_message_bytes = 512U;
    inline constexpr std::size_t k_max_binary_bytes = 1024U;

    inline constexpr std::size_t k_policy_component_capacity = 32U;
    inline constexpr std::size_t k_policy_override_count = 4U;
    inline constexpr std::uint32_t k_max_stride = 1'000'000U;
    inline constexpr std::uint8_t k_default_congestion_threshold_pct = 50U;
    inline constexpr std::uint8_t k_max_percentage = 100U;
    inline constexpr std::uint32_t k_default_congestion_info_stride = 16U;
    inline constexpr std::uint16_t k_default_lookbehind_slots = 128U;
    inline constexpr char k_ascii_del = static_cast<char>(0x7F);
    inline constexpr std::chrono::milliseconds k_default_flush_timeout{1000};

    // Value based per component override of the debug stride. An empty component name marks an unused slot.
    // The fixed size character array keeps the struct free of pointers so an external control component
    // can fill it directly from a Varlink, D-Bus or HTTP payload without dangling references.
    struct component_override {
        std::array<char, k_policy_component_capacity> component{};
        std::uint32_t debug_stride{1U};

        [[nodiscard]] friend constexpr auto operator==(
            const component_override&,
            const component_override&
        ) noexcept -> bool = default;
    };

    [[nodiscard]] constexpr auto component_name(const component_override& entry) noexcept -> std::string_view {
        const auto* const end_it = std::ranges::find(entry.component, '\0');
        auto const length = static_cast<std::size_t>(std::ranges::distance(entry.component.begin(), end_it));
        return {entry.component.data(), length};
    }

    // Builds an override for a component name. Returns nullopt when the name is empty or does not fit.
    [[nodiscard]] constexpr auto make_component_override(
        std::string_view name,
        std::uint32_t debug_stride
    ) noexcept -> std::optional<component_override> {
        if (name.empty() || name.size() >= k_policy_component_capacity) {
            return std::nullopt;
        }
        component_override result{};
        std::ranges::copy(name, result.component.begin());
        result.debug_stride = debug_stride;
        return result;
    }

    // Declarative consumer policy. A default constructed policy is the compiled in flight recorder default.
    // Strides use 0 for ring only, 1 for always emit and N for every Nth record of that severity.
    // Records at or above the lookbehind trigger severity always emit and flush the recent history first.
    struct policy {
        std::array<std::uint32_t, k_severity_count> stride_by_level{1U, 1U, 1U, 1U, 1U, 1U, 1U, 0U};

        // When ring backlog exceeds this percentage notice and info records are thinned out.
        std::uint8_t congestion_threshold_pct{k_default_congestion_threshold_pct};
        std::uint32_t congestion_info_stride{k_default_congestion_info_stride};

        severity lookbehind_trigger{severity::error};
        std::uint16_t lookbehind_slots{k_default_lookbehind_slots};

        std::array<component_override, k_policy_override_count> overrides{};

        [[nodiscard]] friend constexpr auto operator==(const policy&, const policy&) noexcept -> bool = default;
    };

    [[nodiscard]] constexpr auto is_valid_component_override(const component_override& entry) noexcept -> bool {
        if (entry.debug_stride > k_max_stride) {
            return false;
        }
        auto const name = component_name(entry);
        if (name.size() >= k_policy_component_capacity) {
            return false;
        }
        return std::ranges::all_of(name, [](char const character) noexcept {
            return character > ' ' && character != '/' && character != '\\' && character != k_ascii_del;
        });
    }

    // Validates a policy before activation so a control component can reject bad external input.
    [[nodiscard]] constexpr auto is_valid_policy(const policy& candidate) noexcept -> bool {
        bool const strides_ok = std::ranges::all_of(
            candidate.stride_by_level,
            [](std::uint32_t const stride) noexcept { return stride <= k_max_stride; }
        );
        if (!strides_ok) {
            return false;
        }
        if (candidate.congestion_threshold_pct > k_max_percentage) {
            return false;
        }
        if (candidate.congestion_info_stride > k_max_stride) {
            return false;
        }
        if (!is_valid_severity(candidate.lookbehind_trigger)) {
            return false;
        }
        if (candidate.lookbehind_slots > k_ring_capacity) {
            return false;
        }
        return std::ranges::all_of(candidate.overrides, is_valid_component_override);
    }

    static_assert(is_valid_policy(policy{}));
    static_assert(policy{}.stride_by_level.at(static_cast<std::size_t>(severity::debug)) == 0U);
    static_assert(!is_valid_policy(policy{.congestion_threshold_pct = k_max_percentage + 1U}));

    // Atomically replaces the active consumer policy. Returns false and keeps the previous policy when the
    // candidate fails is_valid_policy. The last caller wins and no access control is applied.
    [[nodiscard]] auto activate_policy(const policy& candidate) noexcept -> bool;

    // Restores the compiled in default policy.
    void reset_default_policy() noexcept;

    // Returns a copy of the currently active policy for status inspection.
    [[nodiscard]] auto active_policy() noexcept -> policy;

    // Blocks until every record submitted before the call has been processed by the consumer or the
    // timeout expires. Returns true when the consumer caught up. Intended for shutdown and tests.
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout = k_default_flush_timeout) noexcept -> bool;

    // Cumulative counters for health monitoring by an external control component.
    struct statistics {
        std::uint64_t submitted{0U};
        std::uint64_t emitted{0U};
        std::uint64_t lost{0U};
    };

    [[nodiscard]] auto statistics_snapshot() noexcept -> statistics;

    // Strong type wrapping a human-readable message for internal component telemetry.
    struct message {
        std::string_view text;

        constexpr explicit message(std::string_view value) noexcept : text(value) {}
    };

    // Strong type wrapping an arbitrary binary payload slice for internal component telemetry.
    struct binary_payload {
        std::span<const std::byte> bytes;

        constexpr explicit binary_payload(std::span<const std::byte> raw_bytes) noexcept
            : bytes(raw_bytes) {}
    };

    // Wraps any trivially copyable struct or value into a binary_payload without reinterpret_cast at call sites.
    template <typename T>
        requires (std::is_trivially_copyable_v<T> && !std::same_as<std::remove_cvref_t<T>, binary_payload>)
    [[nodiscard]] inline auto as_binary_payload(const T& value) noexcept -> binary_payload {
        return binary_payload{std::as_bytes(std::span<const T, 1U>{&value, 1U})};
    }

    template <typename T, std::size_t Extent>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] inline auto as_binary_payload(std::span<const T, Extent> values) noexcept -> binary_payload {
        return binary_payload{std::as_bytes(values)};
    }

    // Canonical internal log descriptor assembled at compile time from variadic strong log attributes.
    struct log_record {
        severity level{severity::info};
        std::string_view text;
        std::span<const std::byte> binary;
        std::source_location location;
    };

    namespace detail {
        template <typename T, typename... Ts>
        inline constexpr std::size_t count_type_v =
            (std::size_t{0U} + ... +
             (std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<Ts>> ? 1U : 0U));

        template <typename T>
        concept log_attribute =
            std::same_as<std::remove_cvref_t<T>, severity> ||
            std::same_as<std::remove_cvref_t<T>, message> ||
            std::same_as<std::remove_cvref_t<T>, binary_payload>;

        constexpr void assign_log_attribute(log_record& rec, severity level) noexcept {
            rec.level = level;
        }

        constexpr void assign_log_attribute(log_record& rec, const message& msg) noexcept {
            rec.text = msg.text;
        }

        constexpr void assign_log_attribute(log_record& rec, const binary_payload& payload) noexcept {
            rec.binary = payload.bytes;
        }

        void emit_log_record(const log_record& record) noexcept;

        [[nodiscard]] auto capture_current_stacktrace(
            std::span<std::uintptr_t> frame_buffer
        ) noexcept -> std::size_t;

        // Records the consumer hands to the sink after policy filtering. All views are only valid for
        // the duration of the callback.
        struct emitted_record {
            severity level{severity::info};
            std::string_view component;
            std::string_view text;
            std::span<const std::byte> binary;
            const catalog_entry* entry{nullptr};
            std::string_view label;
            const char* file{nullptr};
            const char* function{nullptr};
            std::uint32_t line{0U};
            std::uint32_t thread_id{0U};
            std::uint64_t sequence{0U};
            std::uint64_t monotonic_ns{0U};
            bool lookbehind{false};
        };

        // Replaceable destination for emitted records. The default destination is systemd-journald.
        // The callback runs on the consumer thread and must not log.
        using sink_callback = void (*)(void* context, const emitted_record& record) noexcept;

        struct record_sink {
            sink_callback callback{nullptr};
            void* context{nullptr};
        };

        // Redirects emitted records, for example into a capture buffer in unit tests.
        void set_record_sink(record_sink sink) noexcept;
        void reset_record_sink() noexcept;

        // Drains the consumer and excludes every record submitted so far from future lookbehind flushes.
        void drop_history() noexcept;

        // Enqueues a catalog record. The label must have static storage duration.
        void submit_catalog_record(
            severity level,
            const catalog_entry& entry,
            std::string_view label,
            std::string_view detail_text,
            std::span<const std::byte> binary,
            std::source_location location
        ) noexcept;
    } // namespace detail

    // Validates that a variadic log call contains exactly one severity and at least one message or binary_payload.
    template <typename... Args>
    concept valid_log_pack =
        (sizeof...(Args) >= 2U) &&
        (sizeof...(Args) <= 3U) &&
        (detail::log_attribute<Args> && ...) &&
        (detail::count_type_v<severity, Args...> == 1U) &&
        (detail::count_type_v<message, Args...> <= 1U) &&
        (detail::count_type_v<binary_payload, Args...> <= 1U) &&
        (detail::count_type_v<message, Args...> == 1U ||
         detail::count_type_v<binary_payload, Args...> == 1U);

    // Folds order-independent strong log attributes into a canonical log_record at compile time.
    template <typename... Args>
        requires valid_log_pack<Args...>
    [[nodiscard]] constexpr auto make_log_record(
        std::source_location location,
        const Args&... args
    ) noexcept -> log_record {
        log_record record{};
        record.location = location;
        (detail::assign_log_attribute(record, args), ...);
        return record;
    }

    // Emits an order-independent internal component diagnostic event across any of the 8 Syslog severities.
    // Deduces the COMPONENT field automatically from caller source location under the physical one name rule.
    template <typename... Args>
        requires valid_log_pack<Args...>
    struct log {
        explicit log(
            const Args&... args,
            std::source_location location = std::source_location::current()
        ) noexcept {
            detail::emit_log_record(make_log_record(location, args...));
        }
    };

    template <typename... Args>
    log(const Args&...) -> log<Args...>;

    // Exception carrier thrown by internal component code to select a specific boundary catalog_entry
    // and freeze the binary stacktrace at the exact throw point before stack unwinding begins.
    class catalog_exception final {
    public:
        constexpr explicit catalog_exception(
            const catalog_entry& specific_entry,
            std::string_view reason = {},
            std::source_location throw_location = std::source_location::current()
        ) noexcept
            : _entry(&specific_entry),
              _location(throw_location) {
            std::size_t const copy_len = std::min(reason.size(), k_max_exception_reason_length);
            std::ranges::copy(reason.substr(0U, copy_len), _reason.begin());
            _reason.at(copy_len) = '\0';
            _reason_len = copy_len;

            if !consteval {
                _frame_count = detail::capture_current_stacktrace(_frames);
            }
        }

        [[nodiscard]] constexpr auto entry() const noexcept -> const catalog_entry& {
            return *_entry;
        }

        [[nodiscard]] constexpr auto reason() const noexcept -> std::string_view {
            return {_reason.data(), _reason_len};
        }

        [[nodiscard]] constexpr auto where() const noexcept -> std::source_location {
            return _location;
        }

        [[nodiscard]] constexpr auto trace_addresses() const noexcept -> std::span<const std::uintptr_t> {
            return {_frames.data(), _frame_count};
        }

        [[nodiscard]] auto trace_bytes() const noexcept -> std::span<const std::byte> {
            return std::as_bytes(trace_addresses());
        }

        [[nodiscard]] constexpr auto what() const noexcept -> const char* {
            return _reason_len > 0U ? _reason.data() : _entry->summary.data();
        }

    private:
        const catalog_entry* _entry;
        std::source_location _location;
        std::array<std::uintptr_t, k_max_stacktrace_frames> _frames{};
        std::size_t _frame_count{0U};
        std::array<char, k_max_exception_reason_length + 1U> _reason{};
        std::size_t _reason_len{0U};
    };

    // Lippincott function to be called inside a catch (...) block at a component's noexcept boundary.
    // Rethrows the active exception internally, selects either the thrown catalog_exception entry or
    // the mandatory default_entry, enqueues a binary stacktrace payload for systemd-journald,
    // and returns false so the boundary can return an error status without leaking exceptions.
    [[nodiscard]] auto catch_boundary_exception(
        const catalog_entry& default_entry,
        std::source_location location = std::source_location::current()
    ) noexcept -> bool;

    // Convenience overload enabled only when a component's struct catalog contains a single catalog_entry.
    // Automatically extracts the lone catalog entry at compile time without requiring the developer to name it.
    template <single_entry_catalog Catalog>
    [[nodiscard]] inline auto catch_boundary_exception(
        const Catalog& component_catalog = Catalog{},
        std::source_location location = std::source_location::current()
    ) noexcept -> bool {
        auto const [only_entry] = component_catalog;
        return catch_boundary_exception(only_entry, location);
    }

} // namespace target_layout_extended::journal
