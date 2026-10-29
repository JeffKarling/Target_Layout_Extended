#include <db_export/db_export.hpp>
#include <item_schema/item_schema.pb.h>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace target_layout_extended::db_export {

    auto format_database_snapshot(
        const std::filesystem::path& db_path,
        std::string& formatted_output
    ) noexcept -> bool {
        try {
            if (db_path.empty()) {
                throw std::invalid_argument("database path is empty");
            }

            std::ifstream input(db_path.string(), std::ios::in | std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("failed to open database file: " + db_path.string());
            }

            ItemDatabase db;
            if (!db.ParseFromIstream(&input)) {
                throw std::runtime_error("failed to parse protobuf database: " + db_path.string());
            }

            std::ostringstream out;
            out << "Items Database Snapshot (" << db.items_size() << " records)\n";
            out << "----------------------------------------\n";
            for (int i = 0; i < db.items_size(); ++i) {
                auto const& item = db.items(i);
                out << "  Item ID: " << item.id()
                    << " | Name: " << item.name()
                    << " | Value: " << item.value() << "\n";
            }
            out << "----------------------------------------\n";
            formatted_output = out.str();
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_export_failed);
        }
    }

} // namespace target_layout_extended::db_export
