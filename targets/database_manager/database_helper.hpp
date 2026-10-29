#pragma once

#include <filesystem>

namespace target_layout_extended::database_manager::detail {

    // Private helper declarations hidden from consumers of database_manager.
    [[nodiscard]] auto is_writable_path(const std::filesystem::path& path) noexcept -> bool;
    [[nodiscard]] auto file_exists_and_is_readable(const std::filesystem::path& path) noexcept -> bool;

} // namespace target_layout_extended::database_manager::detail
