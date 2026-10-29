#include <cstddef>
#include <database_manager/database_manager.hpp>
#include <db_export/db_export.hpp>
#include <filesystem>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>
#include <string>
#include <system_error>
#include <tbb_algos/tbb_algos.hpp>
#include <unistd.h>
#include <vector>

int main() {
  namespace dm = target_layout_extended::database_manager;
  namespace de = target_layout_extended::db_export;
  namespace jrn = target_layout_extended::journal;
  namespace ta = target_layout_extended::tbb_algos;

  std::filesystem::path const db_path =
      std::filesystem::temp_directory_path() /
      ("showcase_database_" + std::to_string(::getpid()) + ".pb");

  std::error_code err_code;
  std::filesystem::remove(db_path, err_code);

  auto store = dm::database_store::open(db_path);
  if (!store) {
    jrn::log_error("Failed to open database_manager");
    return 1;
  }

  std::vector<int> numbers = {10, 20, 30, 40, 50, 60, 70, 80};
  if (!ta::parallel_double(numbers)) {
    jrn::log_error("Failed to execute tbb_algos::parallel_double");
    return 1;
  }

  for (std::size_t i = 0U; i < numbers.size(); ++i) {
    target_layout_extended::Item item;
    item.set_id("ITEM_" + std::to_string(i + 1U));
    item.set_name("Component Item " + std::to_string(i + 1U));
    item.set_value(static_cast<double>(numbers.at(i)));
    if (!store->add_item(item)) {
      jrn::log_error("Failed to add item to database_manager");
      return 1;
    }
  }

  if (!store->save()) {
    jrn::log_error("Failed to save database snapshot");
    return 1;
  }

  auto const formatted = de::format_database_snapshot(db_path);
  if (!formatted) {
    jrn::log_error("Failed to format database snapshot");
    return 1;
  }

  jrn::log_info(*formatted);
  std::filesystem::remove(db_path, err_code);
  return 0;
}
