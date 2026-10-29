#pragma once

#include <algorithm>
#include <cstddef>
#include <source_location>
#include <string_view>

namespace target_layout_extended::journal {

    // Represents a documented systemd journald catalog entry at a component boundary.
    // Every public MESSAGE_ID corresponds to an entry in the component's <name>.catalog file.
    struct catalog_entry {
        std::string_view message_id;
        std::string_view component;
        std::string_view summary;
    };

    namespace catalog {
        inline constexpr catalog_entry k_unhandled_boundary_exception{
            .message_id = "7a1d4f90e3c84a21b5809e41d2a7c001",
            .component = "journal",
            .summary = "Unhandled exception caught at component boundary"
        };
    } // namespace catalog

    inline constexpr std::size_t k_uuid_hex_length = 32U;

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
        while (start < file_path.size() && (file_path[start] == '/' || file_path[start] == '\\')) {
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

    // Emits a structured informational event tied to a documented component catalog MESSAGE_ID.
    void log_info(
        const catalog_entry& entry,
        std::string_view detail,
        std::source_location location = std::source_location::current()
    ) noexcept;

    // Emits a structured error event tied to a documented component catalog MESSAGE_ID.
    void log_error(
        const catalog_entry& entry,
        std::string_view detail,
        std::source_location location = std::source_location::current()
    ) noexcept;

    // Emits a non-cataloged structured informational event for internal component diagnostics.
    // The component name is deduced implicitly from caller location under the one name rule.
    void log_info(
        std::string_view message,
        std::source_location location = std::source_location::current()
    ) noexcept;

    // Emits a non-cataloged structured error event for internal operational diagnostics.
    // The component name is deduced implicitly from caller location under the one name rule.
    void log_error(
        std::string_view message,
        std::source_location location = std::source_location::current()
    ) noexcept;

    // Lippincott function to be called inside a catch (...) block at a component's noexcept boundary.
    // Rethrows the active exception internally, extracts its diagnostic message, logs it to
    // systemd-journald with the component's catalog MESSAGE_ID, and returns false so the caller
    // can safely return an error status without leaking exceptions across target boundaries.
    [[nodiscard]] auto catch_boundary_exception(
        const catalog_entry& entry = catalog::k_unhandled_boundary_exception,
        std::source_location location = std::source_location::current()
    ) noexcept -> bool;

} // namespace target_layout_extended::journal
