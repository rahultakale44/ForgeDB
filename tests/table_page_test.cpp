#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/table_page.h"
#include "storage/tuple.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::storage::DiskManager;
using forgedb::storage::Page;
using forgedb::storage::PageId;
using forgedb::storage::Schema;
using forgedb::storage::SlotId;
using forgedb::storage::TablePage;
using forgedb::storage::Tuple;
using forgedb::storage::Value;
using forgedb::storage::ValueType;

class TablePageTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_table_page.db";

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }

        disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
        buffer_pool_ =
            std::make_unique<BufferPoolManager>(10, *disk_manager_);
    }

    void TearDown() override {
        buffer_pool_.reset();
        disk_manager_.reset();

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }
    }

    std::string test_db_path_;
    std::unique_ptr<DiskManager> disk_manager_;
    std::unique_ptr<BufferPoolManager> buffer_pool_;
};

TEST_F(TablePageTest, InitializesPage) {
    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    EXPECT_EQ(table_page.tuple_count(), 0);
    EXPECT_GT(table_page.free_space(), 0);

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, InsertsAndRetrievesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(42));
    tuple1.set_value(1, Value(std::string{"hello"}));

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple1, slot_id));
    EXPECT_EQ(slot_id, 0);
    EXPECT_EQ(table_page.tuple_count(), 1);

    Tuple tuple2(schema);

    EXPECT_TRUE(table_page.get_tuple(slot_id, tuple2, schema));

    EXPECT_EQ(*tuple2.get_value(0).as_int(), 42);
    EXPECT_EQ(*tuple2.get_value(1).as_string(), "hello");

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, InsertsMultipleTuples) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    for (int i = 0; i < 10; ++i) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(i * 10));

        SlotId slot_id = 0;

        EXPECT_TRUE(table_page.insert_tuple(tuple, slot_id));
        EXPECT_EQ(slot_id, i);
    }

    EXPECT_EQ(table_page.tuple_count(), 10);

    for (int i = 0; i < 10; ++i) {
        Tuple tuple(schema);

        EXPECT_TRUE(table_page.get_tuple(i, tuple, schema));
        EXPECT_EQ(*tuple.get_value(0).as_int(), i * 10);
    }

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, DeletesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(100));

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple1, slot_id));
    EXPECT_EQ(table_page.tuple_count(), 1);

    EXPECT_TRUE(table_page.delete_tuple(slot_id));
    EXPECT_EQ(table_page.tuple_count(), 0);

    Tuple tuple2(schema);

    EXPECT_FALSE(table_page.get_tuple(slot_id, tuple2, schema));

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, UpdatesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(10));
    tuple1.set_value(1, Value(std::string{"old"}));

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple1, slot_id));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(20));
    tuple2.set_value(1, Value(std::string{"new"}));

    EXPECT_TRUE(table_page.update_tuple(slot_id, tuple2));

    Tuple tuple3(schema);

    EXPECT_TRUE(table_page.get_tuple(slot_id, tuple3, schema));

    EXPECT_EQ(*tuple3.get_value(0).as_int(), 20);
    EXPECT_EQ(*tuple3.get_value(1).as_string(), "new");

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, ValidatesSlots) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    Tuple tuple(schema);
    tuple.set_value(0, Value(42));

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple, slot_id));

    EXPECT_TRUE(table_page.is_slot_valid(slot_id));
    EXPECT_FALSE(table_page.is_slot_valid(slot_id + 1));

    EXPECT_TRUE(table_page.delete_tuple(slot_id));

    EXPECT_FALSE(table_page.is_slot_valid(slot_id));

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, HandlesFreeSpaceManagement) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    const std::size_t initial_free_space = table_page.free_space();

    Tuple tuple(schema);
    tuple.set_value(0, Value(42));

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple, slot_id));

    const std::size_t after_insert_free_space = table_page.free_space();

    EXPECT_LT(after_insert_free_space, initial_free_space);

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, PersistsTuplesAcrossReopen) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    PageId page_id = 0;

    {
        Page* page = buffer_pool_->new_page(page_id);

        ASSERT_NE(page, nullptr);

        TablePage table_page(*page);
        table_page.init();

        for (int i = 0; i < 5; ++i) {
            Tuple tuple(schema);
            tuple.set_value(0, Value(i * 100));
            tuple.set_value(
                1,
                Value(std::string{"data_"} + std::to_string(i))
            );

            SlotId slot_id = 0;

            EXPECT_TRUE(table_page.insert_tuple(tuple, slot_id));
        }

        buffer_pool_->unpin_page(page_id, true);
        buffer_pool_->flush_all_pages();
    }

    buffer_pool_.reset();
    disk_manager_.reset();

    disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
    buffer_pool_ =
        std::make_unique<BufferPoolManager>(10, *disk_manager_);

    {
        Page* page = buffer_pool_->fetch_page(page_id);

        ASSERT_NE(page, nullptr);

        TablePage table_page(*page);

        EXPECT_EQ(table_page.tuple_count(), 5);

        for (int i = 0; i < 5; ++i) {
            Tuple tuple(schema);

            EXPECT_TRUE(table_page.get_tuple(i, tuple, schema));

            EXPECT_EQ(*tuple.get_value(0).as_int(), i * 100);
            EXPECT_EQ(
                *tuple.get_value(1).as_string(),
                std::string{"data_"} + std::to_string(i)
            );
        }

        buffer_pool_->unpin_page(page_id, false);
    }
}

TEST_F(TablePageTest, HandlesDeleteAndReinsert) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(100));

    SlotId slot_id1 = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple1, slot_id1));
    EXPECT_TRUE(table_page.delete_tuple(slot_id1));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(200));

    SlotId slot_id2 = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple2, slot_id2));
    EXPECT_EQ(table_page.tuple_count(), 1);

    Tuple retrieved(schema);

    EXPECT_TRUE(table_page.get_tuple(slot_id2, retrieved, schema));
    EXPECT_EQ(*retrieved.get_value(0).as_int(), 200);

    buffer_pool_->unpin_page(page_id, true);
}

TEST_F(TablePageTest, HandlesMultipleUpdates) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    PageId page_id = 0;
    Page* page = buffer_pool_->new_page(page_id);

    ASSERT_NE(page, nullptr);

    TablePage table_page(*page);
    table_page.init();

    Tuple tuple(schema);
    tuple.set_value(0, Value(1));

    SlotId slot_id = 0;

    EXPECT_TRUE(table_page.insert_tuple(tuple, slot_id));

    for (int i = 2; i <= 10; ++i) {
        Tuple updated(schema);
        updated.set_value(0, Value(i));

        EXPECT_TRUE(table_page.update_tuple(slot_id, updated));
    }

    Tuple final_tuple(schema);

    EXPECT_TRUE(table_page.get_tuple(slot_id, final_tuple, schema));
    EXPECT_EQ(*final_tuple.get_value(0).as_int(), 10);

    buffer_pool_->unpin_page(page_id, true);
}

}  // namespace
