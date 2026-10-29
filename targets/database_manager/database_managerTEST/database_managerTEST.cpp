#include <gtest/gtest.h>
#include <database_manager/database_manager.hpp>
#include <filesystem>

namespace dm = target_layout_extended::database_manager;

TEST(DatabaseManagerTest, CatalogIdsAreValidHexUuids) {
    EXPECT_TRUE(target_layout_extended::journal::is_valid_message_id(dm::catalog::k_init_failed.message_id));
    EXPECT_TRUE(target_layout_extended::journal::is_valid_message_id(dm::catalog::k_add_item_failed.message_id));
    EXPECT_TRUE(target_layout_extended::journal::is_valid_message_id(dm::catalog::k_save_failed.message_id));
    EXPECT_TRUE(target_layout_extended::journal::is_valid_message_id(dm::catalog::k_load_failed.message_id));
}

TEST(DatabaseManagerTest, AddRetrieveAndSaveItemsWorks) {
    std::filesystem::path const test_db = "test_items.pb";
    std::error_code ec;
    std::filesystem::remove(test_db, ec);

    dm::database_store db;
    ASSERT_TRUE(db.initialize(test_db));

    target_layout_extended::Item item;
    item.set_id("ID_001");
    item.set_name("Test Item");
    item.set_value(100.50);

    EXPECT_TRUE(db.add_item(item));

    auto const items = db.get_all_items();
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0].id(), "ID_001");
    EXPECT_EQ(items[0].name(), "Test Item");
    EXPECT_DOUBLE_EQ(items[0].value(), 100.50);

    EXPECT_TRUE(db.save());
    EXPECT_TRUE(std::filesystem::exists(test_db));

    std::filesystem::remove(test_db, ec);
}

TEST(DatabaseManagerTest, BoundaryCatchesInvalidInputsWithoutThrowing) {
    dm::database_store db;
    EXPECT_FALSE(db.initialize(""));

    target_layout_extended::Item empty_id_item;
    empty_id_item.set_name("Missing ID");
    EXPECT_FALSE(db.add_item(empty_id_item));
    EXPECT_FALSE(db.save_to("/non_existent_root_dir_xyz/db.pb"));
}
