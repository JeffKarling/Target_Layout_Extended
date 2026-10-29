#pragma once

#include <filesystem>
#include <string>
#include <journal/journal.hpp>

namespace target_layout_extended::db_export {

    namespace catalog {
        inline constexpr journal::catalog_entry k_export_failed{
            .message_id = "8e4c9201b7a34d568f120c93e5d7b301",
            .component = "db_export",
            .summary = "Database snapshot export failure caught at component boundary"
        };
    } // namespace catalog

    // Reads a serialized Protobuf database from disk and formats a human-readable snapshot.
    [[nodiscard]] auto format_database_snapshot(
        const std::filesystem::path& db_path,
        std::string& formatted_output
    ) noexcept -> bool;

} // namespace target_layout_extended::db_export
