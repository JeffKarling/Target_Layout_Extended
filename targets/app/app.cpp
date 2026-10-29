#include <database_manager/database_manager.hpp>
#include <db_export/db_export.hpp>
#include <tbb_algos/tbb_algos.hpp>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
    namespace dm = target_layout_extended::database_manager;
    namespace de = target_layout_extended::db_export;
    namespace ta = target_layout_extended::tbb_algos;

    std::filesystem::path const db_path = "showcase_database.pb";
    std::error_code ec;
    std::filesystem::remove(db_path, ec);

    dm::database_store store;
    if (!store.initialize(db_path)) {
        std::cerr << "[ERROR] Failed to initialize database_manager.\n";
        return 1;
    }

    std::vector<int> numbers = {10, 20, 30, 40, 50, 60, 70, 80};
    if (!ta::parallel_double(numbers)) {
        std::cerr << "[ERROR] Failed to execute tbb_algos::parallel_double.\n";
        return 1;
    }

    for (std::size_t i = 0U; i < numbers.size(); ++i) {
        target_layout_extended::Item item;
        item.set_id("ITEM_" + std::to_string(i + 1U));
        item.set_name("Component Item " + std::to_string(i + 1U));
        item.set_value(static_cast<double>(numbers[i]));
        if (!store.add_item(item)) {
            std::cerr << "[ERROR] Failed to add item to database_manager.\n";
            return 1;
        }
    }

    if (!store.save()) {
        std::cerr << "[ERROR] Failed to save database snapshot.\n";
        return 1;
    }

    std::string formatted;
    if (!de::format_database_snapshot(db_path, formatted)) {
        std::cerr << "[ERROR] Failed to format database snapshot.\n";
        return 1;
    }

    std::cout << formatted;
    std::filesystem::remove(db_path, ec);
    return 0;
}
