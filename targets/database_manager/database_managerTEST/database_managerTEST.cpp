#include <gtest/gtest.h>
#include <database_manager/database_manager.hpp>
#include <item_proto/item_proto.pb.h>
#include <journal/journal.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace dm = tle::database_manager;

static_assert(tle::journal::is_valid_catalog(dm::k_catalog));
static_assert(!tle::journal::single_entry_catalog<dm::catalog>);

TEST(DatabaseManagerTest, CatalogIdsAreValidHexUuids) {
    EXPECT_TRUE(tle::journal::is_valid_message_id(dm::k_catalog.k_init_failed.message_id));
    EXPECT_TRUE(tle::journal::is_valid_message_id(dm::k_catalog.k_add_item_failed.message_id));
    EXPECT_TRUE(tle::journal::is_valid_message_id(dm::k_catalog.k_save_failed.message_id));
    EXPECT_TRUE(tle::journal::is_valid_message_id(dm::k_catalog.k_load_failed.message_id));
}

TEST(DatabaseManagerTest, AddRetrieveAndSaveItemsWorks) {
    std::filesystem::path const test_db = "test_items.pb";
    std::error_code ec;
    std::filesystem::remove(test_db, ec);

    dm::database_store db;
    ASSERT_TRUE(db.initialize(test_db));

    tle::Item item;
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

    tle::Item empty_id_item;
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

    tle::Item item;
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

TEST(DatabaseManagerTest, ViewItemsInspectsUnderLockAndRangesFindUpdatesExistingItem) {
    std::filesystem::path const test_db = "test_view_items.pb";
    std::error_code ec;
    std::filesystem::remove(test_db, ec);

    auto store = dm::database_store::open(test_db);
    ASSERT_TRUE(store.has_value());

    tle::Item item1;
    item1.set_id("ITEM_RANGES_01");
    item1.set_name("Initial Name");
    item1.set_value(10.0);
    EXPECT_TRUE(store->add_item(item1));

    tle::Item item1_updated;
    item1_updated.set_id("ITEM_RANGES_01");
    item1_updated.set_name("Updated Name");
    item1_updated.set_value(99.0);
    EXPECT_TRUE(store->add_item(item1_updated));

    std::size_t visited_count = 0U;
    std::string found_name;
    double found_value = 0.0;

    store->view_items([&](auto const& items_range) {
        for (auto const& item : items_range) {
            ++visited_count;
            found_name = item.name();
            found_value = item.value();
        }
    });

    EXPECT_EQ(visited_count, 1U);
    EXPECT_EQ(found_name, "Updated Name");
    EXPECT_DOUBLE_EQ(found_value, 99.0);

    std::filesystem::remove(test_db, ec);
}

