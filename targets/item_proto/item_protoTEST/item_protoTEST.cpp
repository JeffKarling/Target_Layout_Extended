#include <gtest/gtest.h>
#include <item_proto/item_proto.pb.h>
#include <string>

TEST(ItemProtoTest, SerializesAndParsesProtobufMessages) {
    target_layout_extended::ItemDatabase source_db;
    auto* item = source_db.add_items();
    item->set_id("PROTO_01");
    item->set_name("Generated Proto Item");
    item->set_value(42.5);

    std::string serialized;
    ASSERT_TRUE(source_db.SerializeToString(&serialized));

    target_layout_extended::ItemDatabase parsed_db;
    ASSERT_TRUE(parsed_db.ParseFromString(serialized));
    ASSERT_EQ(parsed_db.items_size(), 1);
    EXPECT_EQ(parsed_db.items(0).id(), "PROTO_01");
    EXPECT_EQ(parsed_db.items(0).name(), "Generated Proto Item");
    EXPECT_DOUBLE_EQ(parsed_db.items(0).value(), 42.5);
}
