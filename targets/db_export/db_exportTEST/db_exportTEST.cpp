#include <gtest/gtest.h>
#include <database_manager/database_manager.hpp>
#include <db_export/db_export.hpp>
#include <item_proto/item_proto.pb.h>
#include <filesystem>
#include <string>
#include <system_error>

namespace dm = target_layout_extended::database_manager;
namespace de = target_layout_extended::db_export;

TEST(DbExportTest, FormatsSavedDatabaseSnapshot) {
    std::filesystem::path const test_db = "test_export.pb";
    std::error_code ec;
    std::filesystem::remove(test_db, ec);

    dm::database_store store;
    ASSERT_TRUE(store.initialize(test_db));

    target_layout_extended::Item item;
    item.set_id("EXP_01");
    item.set_name("Exported Item");
    item.set_value(77.0);
    ASSERT_TRUE(store.add_item(item));
    ASSERT_TRUE(store.save());

    auto const output = de::format_database_snapshot(test_db);
    ASSERT_TRUE(output.has_value());
    EXPECT_NE(output->find("EXP_01"), std::string::npos);

    std::filesystem::remove(test_db, ec);
}

TEST(DbExportTest, BoundaryCatchesMissingFileWithoutThrowing) {
    auto const output = de::format_database_snapshot("non_existent_db_file.pb");
    EXPECT_FALSE(output.has_value());
}
