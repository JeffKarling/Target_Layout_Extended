#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <ranges>
#include <vector>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>

namespace tle::database_manager {

    // Public catalog IDs documented in targets/database_manager/database_manager.catalog.
    struct catalog {
        journal::catalog_entry k_init_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e101",
            .component = "database_manager",
            .summary = "Database initialization failure caught at component boundary"
        };
        journal::catalog_entry k_add_item_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e102",
            .component = "database_manager",
            .summary = "Item insertion failure caught at component boundary"
        };
        journal::catalog_entry k_save_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e103",
            .component = "database_manager",
            .summary = "Database serialization failure caught at component boundary"
        };
        journal::catalog_entry k_load_failed{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e104",
            .component = "database_manager",
            .summary = "Database deserialization failure caught at component boundary"
        };
    };
    inline constexpr catalog k_catalog{};

    class database_store {
    public:
        database_store() = default;
        ~database_store() = default;
        database_store(const database_store&) = delete;
        auto operator=(const database_store&) -> database_store& = delete;
        database_store(database_store&& other) noexcept;
        auto operator=(database_store&& other) noexcept -> database_store&;

        static auto open(const std::filesystem::path& db_path) noexcept -> std::optional<database_store>;
        explicit database_store(std::filesystem::path db_path) noexcept;

        [[nodiscard]] auto is_open() const noexcept -> bool;

        [[nodiscard]] auto initialize(const std::filesystem::path& db_path) noexcept -> bool;
        [[nodiscard]] auto add_item(const Item& item) noexcept -> bool;
        [[nodiscard]] auto save() const noexcept -> bool;
        [[nodiscard]] auto save_to(const std::filesystem::path& path) const noexcept -> bool;
        [[nodiscard]] auto load() noexcept -> bool;
        [[nodiscard]] auto get_all_items() const noexcept -> std::vector<Item>;

        // Safely inspects items under the internal mutex using a ranges subrange view without copying elements.
        template <typename Visitor>
        void view_items(Visitor&& visitor) const noexcept {
            std::scoped_lock const lock(_db_mutex);
            visitor(std::ranges::subrange(_database.items().begin(), _database.items().end()));
        }

    private:
        std::filesystem::path _db_path;
        ItemDatabase _database;
        mutable std::mutex _db_mutex;
    };

} // namespace tle::database_manager
