#include <database_manager/database_manager.hpp>
#include "database_helper.hpp"
#include <fstream>
#include <stdexcept>
#include <string>

namespace target_layout_extended::database_manager {

    auto database_store::initialize(const std::filesystem::path& db_path) noexcept -> bool {
        try {
            if (db_path.empty()) {
                throw std::invalid_argument("database path must not be empty");
            }
            std::lock_guard<std::mutex> const lock(_db_mutex);
            _db_path = db_path;
            return load();
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_init_failed);
        }
    }

    auto database_store::add_item(const Item& item) noexcept -> bool {
        try {
            if (item.id().empty()) {
                throw std::invalid_argument("item id must not be empty");
            }
            std::lock_guard<std::mutex> const lock(_db_mutex);

            for (int i = 0; i < _database.items_size(); ++i) {
                if (_database.items(i).id() == item.id()) {
                    *(_database.mutable_items(i)) = item;
                    return true;
                }
            }

            *(_database.add_items()) = item;
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_add_item_failed);
        }
    }

    auto database_store::save() const noexcept -> bool {
        return save_to(_db_path);
    }

    auto database_store::save_to(const std::filesystem::path& path) const noexcept -> bool {
        try {
            if (!detail::is_writable_path(path)) {
                throw std::runtime_error("destination path is not writable: " + path.string());
            }

            std::ofstream out(path.string(), std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                throw std::runtime_error("failed to open destination file: " + path.string());
            }
            if (!_database.SerializeToOstream(&out)) {
                throw std::runtime_error("protobuf serialization failed for: " + path.string());
            }
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_save_failed);
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
                throw std::runtime_error("failed to parse binary database: " + _db_path.string());
            }
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_load_failed);
        }
    }

    auto database_store::get_all_items() const noexcept -> std::vector<Item> {
        try {
            std::lock_guard<std::mutex> const lock(_db_mutex);
            std::vector<Item> result;
            result.reserve(static_cast<std::size_t>(_database.items_size()));
            for (int i = 0; i < _database.items_size(); ++i) {
                result.push_back(_database.items(i));
            }
            return result;
        } catch (...) {
            (void)journal::catch_boundary_exception(catalog::k_load_failed);
            return {};
        }
    }

} // namespace target_layout_extended::database_manager
