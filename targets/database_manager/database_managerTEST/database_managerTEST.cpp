#include <gtest/gtest.h>
#include <database_manager/database_manager.hpp>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

TEST(DatabaseManagerTest, FactoryOpenAndMoveSemanticsWork) {
    std::filesystem::path const test_db = "test_open_items.pb";
    std::error_code ec;
    std::filesystem::remove(test_db, ec);

    auto maybe_store = dm::database_store::open(test_db);
    ASSERT_TRUE(maybe_store.has_value());
    EXPECT_TRUE(maybe_store->is_open());

    target_layout_extended::Item item;
    item.set_id("ID_OPEN_01");
    item.set_name("Open Item");
    item.set_value(42.0);
    EXPECT_TRUE(maybe_store->add_item(item));
    EXPECT_TRUE(maybe_store->save());

    dm::database_store moved_store = std::move(*maybe_store);
    EXPECT_TRUE(moved_store.is_open());
    auto const items = moved_store.get_all_items();
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0].id(), "ID_OPEN_01");

    dm::database_store assigned_store;
    assigned_store = std::move(moved_store);
    EXPECT_TRUE(assigned_store.is_open());
    EXPECT_EQ(assigned_store.get_all_items().size(), 1U);

    auto const invalid_store = dm::database_store::open("");
    EXPECT_FALSE(invalid_store.has_value());

    std::filesystem::remove(test_db, ec);
}
