#include "database_helper.hpp"
#include <fstream>
#include <system_error>

namespace target_layout_extended::database_manager::detail {

    auto is_writable_path(const std::filesystem::path& path) noexcept -> bool {
        if (path.empty()) {
            return false;
        }
        auto parent = path.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        std::error_code ec;
        if (!std::filesystem::exists(parent, ec)) {
            return false;
        }
        return (std::filesystem::status(parent, ec).permissions() & std::filesystem::perms::owner_write) !=
               std::filesystem::perms::none;
    }

    auto file_exists_and_is_readable(const std::filesystem::path& path) noexcept -> bool {
        if (path.empty()) {
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
            return false;
        }
        std::ifstream const file(path.string(), std::ios::in | std::ios::binary);
        return file.is_open();
    }

} // namespace target_layout_extended::database_manager::detail
