#include <algorithm>
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "storage/disk_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::catalog::Catalog;
using forgedb::catalog::ColumnDefinition;
using forgedb::catalog::IndexDefinition;
using forgedb::catalog::TableMetadata;
using forgedb::storage::DiskManager;
using forgedb::storage::PageId;
using forgedb::storage::ValueType;

class CatalogTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_catalog.db";

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

TEST_F(CatalogTest, CreatesEmptyCatalog) {
    Catalog catalog(*buffer_pool_);

    EXPECT_GE(catalog.catalog_page_id(), 0);
    EXPECT_TRUE(catalog.list_tables().empty());
}

TEST_F(CatalogTest, CreatesTable) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("name", ValueType::VARCHAR, 50);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    const auto metadata = catalog.get_table("users");

    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->table_name, "users");
    EXPECT_EQ(metadata->columns.size(), 2);
    EXPECT_EQ(metadata->columns[0].name, "id");
    EXPECT_EQ(metadata->columns[0].type, ValueType::INTEGER);
    EXPECT_EQ(metadata->columns[1].name, "name");
    EXPECT_EQ(metadata->columns[1].type, ValueType::VARCHAR);
    EXPECT_EQ(metadata->columns[1].max_length, 50);
    EXPECT_EQ(metadata->first_page_id, 100);
}

TEST_F(CatalogTest, RejectsDuplicateTable) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));
    EXPECT_FALSE(catalog.create_table("users", columns, 200));
}

TEST_F(CatalogTest, DropsTable) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));
    EXPECT_TRUE(catalog.get_table("users").has_value());

    EXPECT_TRUE(catalog.drop_table("users"));
    EXPECT_FALSE(catalog.get_table("users").has_value());
}

TEST_F(CatalogTest, DropNonExistentTableFails) {
    Catalog catalog(*buffer_pool_);

    EXPECT_FALSE(catalog.drop_table("nonexistent"));
}

TEST_F(CatalogTest, ListsTables) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));
    EXPECT_TRUE(catalog.create_table("products", columns, 200));
    EXPECT_TRUE(catalog.create_table("orders", columns, 300));

    auto tables = catalog.list_tables();

    EXPECT_EQ(tables.size(), 3);

    std::sort(tables.begin(), tables.end());

    EXPECT_EQ(tables[0], "orders");
    EXPECT_EQ(tables[1], "products");
    EXPECT_EQ(tables[2], "users");
}

TEST_F(CatalogTest, PersistsAcrossReopen) {
    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("name", ValueType::VARCHAR, 100);
    columns.emplace_back("active", ValueType::BOOLEAN);

    PageId catalog_page_id = 0;

    {
        Catalog catalog(*buffer_pool_);
        catalog_page_id = catalog.catalog_page_id();

        EXPECT_TRUE(catalog.create_table("users", columns, 100));
        EXPECT_TRUE(catalog.create_table("products", columns, 200));

        buffer_pool_->flush_all_pages();
    }

    buffer_pool_.reset();
    disk_manager_.reset();

    disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
    buffer_pool_ =
        std::make_unique<BufferPoolManager>(20, *disk_manager_);

    {
        Catalog catalog(*buffer_pool_, catalog_page_id);

        auto tables = catalog.list_tables();
        EXPECT_EQ(tables.size(), 2);

        const auto users_meta = catalog.get_table("users");
        ASSERT_TRUE(users_meta.has_value());
        EXPECT_EQ(users_meta->table_name, "users");
        EXPECT_EQ(users_meta->columns.size(), 3);
        EXPECT_EQ(users_meta->columns[0].name, "id");
        EXPECT_EQ(users_meta->columns[1].name, "name");
        EXPECT_EQ(users_meta->columns[1].max_length, 100);
        EXPECT_EQ(users_meta->columns[2].name, "active");
        EXPECT_EQ(users_meta->first_page_id, 100);

        const auto products_meta = catalog.get_table("products");
        ASSERT_TRUE(products_meta.has_value());
        EXPECT_EQ(products_meta->first_page_id, 200);
    }
}

TEST_F(CatalogTest, AddsIndex) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("email", ValueType::VARCHAR, 100);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    EXPECT_TRUE(catalog.add_index("users", "idx_id", 0, 500));

    const auto metadata = catalog.get_table("users");
    ASSERT_TRUE(metadata.has_value());
    ASSERT_EQ(metadata->indexes.size(), 1);
    EXPECT_EQ(metadata->indexes[0].index_name, "idx_id");
    EXPECT_EQ(metadata->indexes[0].column_index, 0);
    EXPECT_EQ(metadata->indexes[0].root_page_id, 500);
}

TEST_F(CatalogTest, AddIndexToNonExistentTableFails) {
    Catalog catalog(*buffer_pool_);

    EXPECT_FALSE(catalog.add_index("nonexistent", "idx_id", 0, 500));
}

TEST_F(CatalogTest, RejectsDuplicateIndexName) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("email", ValueType::VARCHAR, 100);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    EXPECT_TRUE(catalog.add_index("users", "idx_id", 0, 500));
    EXPECT_FALSE(catalog.add_index("users", "idx_id", 1, 600));
}

TEST_F(CatalogTest, RemovesIndex) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("email", ValueType::VARCHAR, 100);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));
    EXPECT_TRUE(catalog.add_index("users", "idx_id", 0, 500));

    const auto metadata1 = catalog.get_table("users");
    ASSERT_TRUE(metadata1.has_value());
    EXPECT_EQ(metadata1->indexes.size(), 1);

    EXPECT_TRUE(catalog.remove_index("users", "idx_id"));

    const auto metadata2 = catalog.get_table("users");
    ASSERT_TRUE(metadata2.has_value());
    EXPECT_EQ(metadata2->indexes.size(), 0);
}

TEST_F(CatalogTest, RemoveNonExistentIndexFails) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    EXPECT_FALSE(catalog.remove_index("users", "nonexistent"));
}

TEST_F(CatalogTest, PersistsIndexesAcrossReopen) {
    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("email", ValueType::VARCHAR, 100);

    PageId catalog_page_id = 0;

    {
        Catalog catalog(*buffer_pool_);
        catalog_page_id = catalog.catalog_page_id();

        EXPECT_TRUE(catalog.create_table("users", columns, 100));
        EXPECT_TRUE(catalog.add_index("users", "idx_id", 0, 500));
        EXPECT_TRUE(catalog.add_index("users", "idx_email", 1, 600));

        buffer_pool_->flush_all_pages();
    }

    buffer_pool_.reset();
    disk_manager_.reset();

    disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
    buffer_pool_ =
        std::make_unique<BufferPoolManager>(20, *disk_manager_);

    {
        Catalog catalog(*buffer_pool_, catalog_page_id);

        const auto metadata = catalog.get_table("users");
        ASSERT_TRUE(metadata.has_value());
        ASSERT_EQ(metadata->indexes.size(), 2);

        EXPECT_EQ(metadata->indexes[0].index_name, "idx_id");
        EXPECT_EQ(metadata->indexes[0].column_index, 0);
        EXPECT_EQ(metadata->indexes[0].root_page_id, 500);

        EXPECT_EQ(metadata->indexes[1].index_name, "idx_email");
        EXPECT_EQ(metadata->indexes[1].column_index, 1);
        EXPECT_EQ(metadata->indexes[1].root_page_id, 600);
    }
}

TEST_F(CatalogTest, ConvertsMetadataToSchema) {
    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("name", ValueType::VARCHAR, 50);
    columns.emplace_back("active", ValueType::BOOLEAN);

    TableMetadata metadata("users", columns, 100);

    const auto schema = metadata.to_schema();

    EXPECT_EQ(schema.column_count(), 3);
    EXPECT_EQ(schema.column_type(0), ValueType::INTEGER);
    EXPECT_EQ(schema.column_type(1), ValueType::VARCHAR);
    EXPECT_EQ(schema.column_max_length(1), 50);
    EXPECT_EQ(schema.column_type(2), ValueType::BOOLEAN);
}

TEST_F(CatalogTest, HandlesMultipleIndexesOnSameTable) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER);
    columns.emplace_back("email", ValueType::VARCHAR, 100);
    columns.emplace_back("age", ValueType::INTEGER);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    EXPECT_TRUE(catalog.add_index("users", "idx_id", 0, 500));
    EXPECT_TRUE(catalog.add_index("users", "idx_email", 1, 600));
    EXPECT_TRUE(catalog.add_index("users", "idx_age", 2, 700));

    const auto metadata = catalog.get_table("users");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->indexes.size(), 3);
}

TEST_F(CatalogTest, HandlesNullableColumns) {
    Catalog catalog(*buffer_pool_);

    std::vector<ColumnDefinition> columns;
    columns.emplace_back("id", ValueType::INTEGER, 0, false);
    columns.emplace_back("name", ValueType::VARCHAR, 50, true);

    EXPECT_TRUE(catalog.create_table("users", columns, 100));

    const auto metadata = catalog.get_table("users");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->columns.size(), 2);
    EXPECT_FALSE(metadata->columns[0].nullable);
    EXPECT_TRUE(metadata->columns[1].nullable);
}

}  // namespace

