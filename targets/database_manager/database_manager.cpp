#include <database_manager/database_manager.hpp>
#include <database_helper.hpp>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace tle::database_manager {

    database_store::database_store(std::filesystem::path db_path) noexcept
        : _db_path(std::move(db_path)) {
        if (!_db_path.empty()) {
            (void)load();
        }
    }

    database_store::database_store(database_store&& other) noexcept {
        std::scoped_lock const lock(other._db_mutex);
        _db_path = std::move(other._db_path);
        _database = std::move(other._database);
    }

    auto database_store::operator=(database_store&& other) noexcept -> database_store& {
        if (this != &other) {
            std::scoped_lock const lock(_db_mutex, other._db_mutex);
            _db_path = std::move(other._db_path);
            _database = std::move(other._database);
        }
        return *this;
    }

    auto database_store::open(const std::filesystem::path& db_path) noexcept -> std::optional<database_store> {
        try {
            if (db_path.empty()) {
                throw journal::catalog_exception(k_catalog.k_init_failed, "database path must not be empty");
            }
            database_store store;
            if (!store.initialize(db_path)) {
                return std::nullopt;
            }
            return store;
        } catch (...) {
            (void)journal::catch_boundary_exception(k_catalog.k_init_failed);
            return std::nullopt;
        }
    }

    auto database_store::is_open() const noexcept -> bool {
        std::scoped_lock const lock(_db_mutex);
        return !_db_path.empty();
    }

    auto database_store::initialize(const std::filesystem::path& db_path) noexcept -> bool {
        try {
            if (db_path.empty()) {
                throw journal::catalog_exception(k_catalog.k_init_failed, "database path must not be empty");
            }
            std::scoped_lock const lock(_db_mutex);
            _db_path = db_path;
            return load();
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog.k_init_failed);
        }
    }

    auto database_store::add_item(const Item& item) noexcept -> bool {
        try {
            if (item.id().empty()) {
                throw journal::catalog_exception(k_catalog.k_add_item_failed, "item id must not be empty");
            }
            std::scoped_lock const lock(_db_mutex);

            auto const items_view = std::ranges::subrange(
                _database.mutable_items()->begin(),
                _database.mutable_items()->end()
            );
            auto const it = std::ranges::find(items_view, item.id(), &Item::id);
            if (it != items_view.end()) {
                *it = item;
                return true;
            }

            *(_database.add_items()) = item;
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog.k_add_item_failed);
        }
    }

    auto database_store::save() const noexcept -> bool {
        return save_to(_db_path);
    }

    auto database_store::save_to(const std::filesystem::path& path) const noexcept -> bool {
        try {
            if (!detail::is_writable_path(path)) {
                throw journal::catalog_exception(
                    k_catalog.k_save_failed,
                    "destination path is not writable: " + path.string()
                );
            }

            std::ofstream out(path.string(), std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                throw journal::catalog_exception(
                    k_catalog.k_save_failed,
                    "failed to open destination file: " + path.string()
                );
            }
            if (!_database.SerializeToOstream(&out)) {
                throw journal::catalog_exception(
                    k_catalog.k_save_failed,
                    "protobuf serialization failed for: " + path.string()
                );
            }
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog.k_save_failed);
        }
    }

    auto database_store::load() noexcept -> bool {
        try {
            if (!detail::file_exists_and_is_readable(_db_path)) {
                _database.Clear();
                return true;
            }

            std::ifstream input(_db_path.string(), std::ios::in | std::ios::binary);
            if (!input.is_open()) {
                _database.Clear();
                return true;
            }
            if (!_database.ParseFromIstream(&input)) {
                _database.Clear();
                throw journal::catalog_exception(
                    k_catalog.k_load_failed,
                    "failed to parse binary database: " + _db_path.string()
                );
            }
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog.k_load_failed);
        }
    }

    auto database_store::get_all_items() const noexcept -> std::vector<Item> {
        try {
            std::scoped_lock const lock(_db_mutex);
            return std::ranges::to<std::vector<Item>>(
                std::ranges::subrange(_database.items().begin(), _database.items().end())
            );
        } catch (...) {
            (void)journal::catch_boundary_exception(k_catalog.k_load_failed);
            return {};
        }
    }

} // namespace tle::database_manager
