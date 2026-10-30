#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>

namespace target_layout_extended::db_export {

    namespace catalog {
        inline constexpr journal::catalog_entry k_export_failed{
            .message_id = "8e4c9201b7a34d568f120c93e5d7b301",
            .component = "db_export",
            .summary = "Database snapshot export failure caught at component boundary"
        };
    } // namespace catalog

    // Formats any range or view of Item records into a human-readable snapshot without intermediate allocations.
    template <std::ranges::input_range R>
    [[nodiscard]] inline auto format_item_records(R&& items) noexcept -> std::string {
        std::size_t count = 0U;
        std::ostringstream body;
        for (auto const& item : items) {
            ++count;
            body << "  Item ID: " << item.id()
                 << " | Name: " << item.name()
                 << " | Value: " << item.value() << "\n";
        }
        std::ostringstream out;
        out << "Items Database Snapshot (" << count << " records)\n";
        out << "----------------------------------------\n";
        out << body.str();
        out << "----------------------------------------\n";
        return out.str();
    }

    // Reads a serialized Protobuf database from disk and formats a human-readable snapshot.
    // Returns std::nullopt if the database cannot be opened or parsed.
    [[nodiscard]] auto format_database_snapshot(
        const std::filesystem::path& db_path
    ) noexcept -> std::optional<std::string>;

} // namespace target_layout_extended::db_export
