#include <gtest/gtest.h>
#include <database_manager/database_manager.hpp>
#include <db_export/db_export.hpp>
#include <item_proto/item_proto.pb.h>
#include <filesystem>
#include <string>
#include <system_error>

#include <ranges>
#include <vector>

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

TEST(DbExportTest, FormatsItemRecordsFromRangesViewWithoutAllocatingContainer) {
    target_layout_extended::Item item1;
    item1.set_id("VIEW_01");
    item1.set_name("Low Value Item");
    item1.set_value(10.0);

    target_layout_extended::Item item2;
    item2.set_id("VIEW_02");
    item2.set_name("High Value Item");
    item2.set_value(95.0);

    std::vector<target_layout_extended::Item> const items = {item1, item2};

    auto high_value_view = items | std::views::filter([](auto const& it) noexcept {
        return it.value() > 50.0;
    });

    std::string const report = de::format_item_records(high_value_view);
    EXPECT_NE(report.find("1 records"), std::string::npos);
    EXPECT_EQ(report.find("VIEW_01"), std::string::npos);
    EXPECT_NE(report.find("VIEW_02"), std::string::npos);
}

TEST(DbExportTest, BoundaryCatchesMissingFileWithoutThrowing) {
    auto const output = de::format_database_snapshot("non_existent_db_file.pb");
    EXPECT_FALSE(output.has_value());
}
