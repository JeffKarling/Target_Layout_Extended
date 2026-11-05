#include <db_export/db_export.hpp>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tle::db_export {

    auto format_database_snapshot(
        const std::filesystem::path& db_path
    ) noexcept -> std::optional<std::string> {
        try {
            if (db_path.empty()) {
                throw std::invalid_argument("database path is empty");
            }

            std::ifstream input(db_path.string(), std::ios::in | std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("failed to open database file: " + db_path.string());
            }

            ItemDatabase database;
            if (!database.ParseFromIstream(&input)) {
                throw std::runtime_error("failed to parse protobuf database: " + db_path.string());
            }

            return format_item_records(database.items());
        } catch (...) {
            (void)journal::catch_boundary_exception(k_catalog);
            return std::nullopt;
        }
    }

} // namespace tle::db_export
