#pragma once

#include <filesystem>
#include <mutex>
#include <vector>
#include <item_schema/item_schema.pb.h>
#include <journal/journal.hpp>

namespace target_layout_extended::database_manager {

    // Public catalog IDs documented in targets/database_manager/database_manager.catalog.
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
        inline constexpr journal::catalog_entry k_save_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e103",
            .component = "database_manager",
            .summary = "Database serialization to disk failed"
        };
        inline constexpr journal::catalog_entry k_load_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e104",
            .component = "database_manager",
            .summary = "Database deserialization from disk failed"
        };
    } // namespace catalog

    class database_store {
    public:
        database_store() = default;
        ~database_store() = default;
        database_store(const database_store&) = delete;
        auto operator=(const database_store&) -> database_store& = delete;
        database_store(database_store&&) = delete;
        auto operator=(database_store&&) -> database_store& = delete;

        [[nodiscard]] auto initialize(const std::filesystem::path& db_path) noexcept -> bool;
        [[nodiscard]] auto add_item(const Item& item) noexcept -> bool;
        [[nodiscard]] auto save() const noexcept -> bool;
        [[nodiscard]] auto save_to(const std::filesystem::path& path) const noexcept -> bool;
        [[nodiscard]] auto load() noexcept -> bool;
        [[nodiscard]] auto get_all_items() const noexcept -> std::vector<Item>;

    private:
        std::filesystem::path _db_path;
        ItemDatabase _database;
        mutable std::mutex _db_mutex;
    };

} // namespace target_layout_extended::database_manager
