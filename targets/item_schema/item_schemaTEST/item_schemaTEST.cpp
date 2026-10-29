#include <gtest/gtest.h>
#include <item_schema/item_schema.pb.h>
#include <string>

TEST(ItemSchemaTest, SerializesAndParsesProtobufMessages) {
    target_layout_extended::ItemDatabase source_db;
    auto* item = source_db.add_items();
    item->set_id("SCHEMA_01");
    item->set_name("Generated Schema Item");
    item->set_value(42.5);

    std::string serialized;
    ASSERT_TRUE(source_db.SerializeToString(&serialized));

    target_layout_extended::ItemDatabase parsed_db;
    ASSERT_TRUE(parsed_db.ParseFromString(serialized));
    ASSERT_EQ(parsed_db.items_size(), 1);
    EXPECT_EQ(parsed_db.items(0).id(), "SCHEMA_01");
    EXPECT_EQ(parsed_db.items(0).name(), "Generated Schema Item");
    EXPECT_DOUBLE_EQ(parsed_db.items(0).value(), 42.5);
}
