#pragma once

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

    // Validates that a catalog MESSAGE_ID is a 32-character lowercase hexadecimal UUID string.
    [[nodiscard]] constexpr auto is_valid_message_id(std::string_view id) noexcept -> bool {
        if (id.size() != 32U) {
            return false;
        }
        for (char const ch : id) {
            bool const is_digit = (ch >= '0' && ch <= '9');
            bool const is_hex_lower = (ch >= 'a' && ch <= 'f');
            if (!is_digit && !is_hex_lower) {
                return false;
            }
        }
        return true;
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
    void log_info(
        std::string_view component,
        std::string_view message,
        std::source_location location = std::source_location::current()
    ) noexcept;

    // Emits a non-cataloged structured error event for internal operational diagnostics.
    void log_error(
        std::string_view component,
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
