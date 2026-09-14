#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"
#include "storage/tuple.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::storage::DiskManager;
using forgedb::storage::IndexedTable;
using forgedb::storage::PageId;
using forgedb::storage::RecordId;
using forgedb::storage::Schema;
using forgedb::storage::TableHeap;
using forgedb::storage::Tuple;
using forgedb::storage::Value;
using forgedb::storage::ValueType;

class TableHeapTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_table_heap.db";

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }

        disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
        buffer_pool_ =
            std::make_unique<BufferPoolManager>(20, *disk_manager_);
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

TEST_F(TableHeapTest, InsertsAndRetrievesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    TableHeap heap(*buffer_pool_, schema);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(42));
    tuple1.set_value(1, Value(std::string{"test"}));

    RecordId rid{};

    EXPECT_TRUE(heap.insert_tuple(tuple1, rid));

    Tuple tuple2(schema);

    EXPECT_TRUE(heap.get_tuple(rid, tuple2));

    EXPECT_EQ(*tuple2.get_value(0).as_int(), 42);
    EXPECT_EQ(*tuple2.get_value(1).as_string(), "test");
}

TEST_F(TableHeapTest, InsertsMultipleTuples) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    TableHeap heap(*buffer_pool_, schema);

    std::vector<RecordId> rids;

    for (int i = 0; i < 20; ++i) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(i * 10));

        RecordId rid{};

        EXPECT_TRUE(heap.insert_tuple(tuple, rid));

        rids.push_back(rid);
    }

    EXPECT_EQ(heap.tuple_count(), 20);

    for (std::size_t i = 0; i < rids.size(); ++i) {
        Tuple tuple(schema);

        EXPECT_TRUE(heap.get_tuple(rids[i], tuple));
        EXPECT_EQ(
            *tuple.get_value(0).as_int(),
            static_cast<std::int32_t>(i * 10)
        );
    }
}

TEST_F(TableHeapTest, DeletesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);

    TableHeap heap(*buffer_pool_, schema);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(100));

    RecordId rid{};

    EXPECT_TRUE(heap.insert_tuple(tuple1, rid));
    EXPECT_EQ(heap.tuple_count(), 1);

    EXPECT_TRUE(heap.delete_tuple(rid));
    EXPECT_EQ(heap.tuple_count(), 0);

    Tuple tuple2(schema);

    EXPECT_FALSE(heap.get_tuple(rid, tuple2));
}

TEST_F(TableHeapTest, UpdatesTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    TableHeap heap(*buffer_pool_, schema);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(10));
    tuple1.set_value(1, Value(std::string{"old"}));

    RecordId rid{};

    EXPECT_TRUE(heap.insert_tuple(tuple1, rid));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(20));
    tuple2.set_value(1, Value(std::string{"new"}));

    EXPECT_TRUE(heap.update_tuple(rid, tuple2));

    Tuple tuple3(schema);

    EXPECT_TRUE(heap.get_tuple(rid, tuple3));

    EXPECT_EQ(*tuple3.get_value(0).as_int(), 20);
    EXPECT_EQ(*tuple3.get_value(1).as_string(), "new");
}

TEST_F(TableHeapTest, PersistsAcrossReopen) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    PageId first_page_id = 0;

    {
        TableHeap heap(*buffer_pool_, schema);

        first_page_id = heap.first_page_id();

        for (int i = 0; i < 10; ++i) {
            Tuple tuple(schema);
            tuple.set_value(0, Value(i * 100));
            tuple.set_value(
                1,
                Value(std::string{"data_"} + std::to_string(i))
            );

            RecordId rid{};

            EXPECT_TRUE(heap.insert_tuple(tuple, rid));
        }

        buffer_pool_->flush_all_pages();
    }

    buffer_pool_.reset();
    disk_manager_.reset();

    disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
    buffer_pool_ =
        std::make_unique<BufferPoolManager>(20, *disk_manager_);

    {
        TableHeap heap(*buffer_pool_, schema, first_page_id);

        EXPECT_EQ(heap.tuple_count(), 10);
    }
}

TEST_F(TableHeapTest, HandlesRecordIdConversion) {
    RecordId rid1{123, 456};

    const auto rid_value = rid1.to_rid();

    const RecordId rid2 = RecordId::from_rid(rid_value);

    EXPECT_EQ(rid1, rid2);
    EXPECT_EQ(rid2.page_id, 123);
    EXPECT_EQ(rid2.slot_id, 456);
}

TEST_F(TableHeapTest, SpansMultiplePages) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 200);

    TableHeap heap(*buffer_pool_, schema);

    std::vector<RecordId> rids;

    for (int i = 0; i < 50; ++i) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(i));
        tuple.set_value(
            1,
            Value(std::string(150, 'x'))
        );

        RecordId rid{};

        EXPECT_TRUE(heap.insert_tuple(tuple, rid));

        rids.push_back(rid);
    }

    EXPECT_EQ(heap.tuple_count(), 50);

    for (std::size_t i = 0; i < rids.size(); ++i) {
        Tuple tuple(schema);

        EXPECT_TRUE(heap.get_tuple(rids[i], tuple));
        EXPECT_EQ(
            *tuple.get_value(0).as_int(),
            static_cast<std::int32_t>(i)
        );
    }
}

TEST_F(TableHeapTest, IndexedTableInsertsAndSearches) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    IndexedTable table(*buffer_pool_, schema, 0);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(10));
    tuple1.set_value(1, Value(std::string{"alice"}));

    EXPECT_TRUE(table.insert(tuple1));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(20));
    tuple2.set_value(1, Value(std::string{"bob"}));

    EXPECT_TRUE(table.insert(tuple2));

    EXPECT_EQ(table.tuple_count(), 2);

    Tuple result(schema);

    EXPECT_TRUE(table.search_by_key(10, result));
    EXPECT_EQ(*result.get_value(0).as_int(), 10);
    EXPECT_EQ(*result.get_value(1).as_string(), "alice");

    EXPECT_TRUE(table.search_by_key(20, result));
    EXPECT_EQ(*result.get_value(0).as_int(), 20);
    EXPECT_EQ(*result.get_value(1).as_string(), "bob");

    EXPECT_FALSE(table.search_by_key(30, result));
}

TEST_F(TableHeapTest, IndexedTableRejectsDuplicateKeys) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    IndexedTable table(*buffer_pool_, schema, 0);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(10));
    tuple1.set_value(1, Value(std::string{"first"}));

    EXPECT_TRUE(table.insert(tuple1));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(10));
    tuple2.set_value(1, Value(std::string{"second"}));

    EXPECT_FALSE(table.insert(tuple2));

    EXPECT_EQ(table.tuple_count(), 1);

    Tuple result(schema);

    EXPECT_TRUE(table.search_by_key(10, result));
    EXPECT_EQ(*result.get_value(1).as_string(), "first");
}

TEST_F(TableHeapTest, IndexedTableDeletes) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    IndexedTable table(*buffer_pool_, schema, 0);

    for (int i = 1; i <= 10; ++i) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(i * 10));
        tuple.set_value(
            1,
            Value(std::string{"data_"} + std::to_string(i))
        );

        EXPECT_TRUE(table.insert(tuple));
    }

    EXPECT_EQ(table.tuple_count(), 10);

    EXPECT_TRUE(table.delete_by_key(50));

    EXPECT_EQ(table.tuple_count(), 9);

    Tuple result(schema);

    EXPECT_FALSE(table.search_by_key(50, result));

    EXPECT_TRUE(table.search_by_key(60, result));
}

TEST_F(TableHeapTest, IndexedTableUpdates) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    IndexedTable table(*buffer_pool_, schema, 0);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(10));
    tuple1.set_value(1, Value(std::string{"old"}));

    EXPECT_TRUE(table.insert(tuple1));

    Tuple tuple2(schema);
    tuple2.set_value(0, Value(10));
    tuple2.set_value(1, Value(std::string{"new"}));

    EXPECT_TRUE(table.update_by_key(10, tuple2));

    Tuple result(schema);

    EXPECT_TRUE(table.search_by_key(10, result));
    EXPECT_EQ(*result.get_value(1).as_string(), "new");
}

TEST_F(TableHeapTest, IndexedTablePersists) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    PageId heap_page_id = 0;
    PageId index_page_id = 0;

    {
        IndexedTable table(*buffer_pool_, schema, 0);

        for (int i = 1; i <= 20; ++i) {
            Tuple tuple(schema);
            tuple.set_value(0, Value(i * 10));
            tuple.set_value(
                1,
                Value(std::string{"data_"} + std::to_string(i))
            );

            EXPECT_TRUE(table.insert(tuple));
        }

        heap_page_id = table.heap_first_page_id();
        index_page_id = table.index_root_page_id();

        buffer_pool_->flush_all_pages();
    }

    buffer_pool_.reset();
    disk_manager_.reset();

    disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
    buffer_pool_ =
        std::make_unique<BufferPoolManager>(20, *disk_manager_);

    {
        IndexedTable table(
            *buffer_pool_,
            schema,
            0,
            heap_page_id,
            index_page_id
        );

        EXPECT_EQ(table.tuple_count(), 20);

        for (int i = 1; i <= 20; ++i) {
            Tuple result(schema);

            EXPECT_TRUE(table.search_by_key(i * 10, result));
            EXPECT_EQ(*result.get_value(0).as_int(), i * 10);
            EXPECT_EQ(
                *result.get_value(1).as_string(),
                std::string{"data_"} + std::to_string(i)
            );
        }

        Tuple final_result(schema);
        EXPECT_FALSE(table.search_by_key(999, final_result));
    }
}

TEST_F(TableHeapTest, IndexedTableHandlesOutOfOrderInsert) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    IndexedTable table(*buffer_pool_, schema, 0);

    const std::vector<int> keys = {50, 10, 30, 70, 20, 60, 40};

    for (int key : keys) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(key));
        tuple.set_value(
            1,
            Value(std::string{"val_"} + std::to_string(key))
        );

        EXPECT_TRUE(table.insert(tuple));
    }

    EXPECT_EQ(table.tuple_count(), keys.size());

    for (int key : keys) {
        Tuple result(schema);

        EXPECT_TRUE(table.search_by_key(key, result));
        EXPECT_EQ(*result.get_value(0).as_int(), key);
        EXPECT_EQ(
            *result.get_value(1).as_string(),
            std::string{"val_"} + std::to_string(key)
        );
    }
}

TEST_F(TableHeapTest, IndexedTableHandlesLargeDataset) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::INTEGER);

    IndexedTable table(*buffer_pool_, schema, 0);

    for (int i = 1; i <= 100; ++i) {
        Tuple tuple(schema);
        tuple.set_value(0, Value(i));
        tuple.set_value(1, Value(i * 100));

        EXPECT_TRUE(table.insert(tuple));
    }

    EXPECT_EQ(table.tuple_count(), 100);

    for (int i = 1; i <= 100; ++i) {
        Tuple result(schema);

        EXPECT_TRUE(table.search_by_key(i, result));
        EXPECT_EQ(*result.get_value(0).as_int(), i);
        EXPECT_EQ(*result.get_value(1).as_int(), i * 100);
    }
}

}  // namespace
