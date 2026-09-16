#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "storage/page.h"
#include "storage/tuple.h"

namespace forgedb::catalog {

struct ColumnDefinition {
    std::string name;
    storage::ValueType type;
    std::size_t max_length;
    bool nullable;

    ColumnDefinition(
        const std::string& name_,
        storage::ValueType type_,
        std::size_t max_length_ = 0,
        bool nullable_ = true
    )
        : name(name_),
          type(type_),
          max_length(max_length_),
          nullable(nullable_) {}
};

struct IndexDefinition {
    std::string index_name;
    std::size_t column_index;
    storage::PageId root_page_id;

    IndexDefinition(
        const std::string& name,
        std::size_t col_idx,
        storage::PageId root_id
    )
        : index_name(name),
          column_index(col_idx),
          root_page_id(root_id) {}
};

struct TableMetadata {
    std::string table_name;
    std::vector<ColumnDefinition> columns;
    storage::PageId first_page_id;
    std::vector<IndexDefinition> indexes;

    TableMetadata() : first_page_id(0) {}

    TableMetadata(
        const std::string& name,
        const std::vector<ColumnDefinition>& cols,
        storage::PageId first_page
    )
        : table_name(name),
          columns(cols),
          first_page_id(first_page) {}

    storage::Schema to_schema() const;
};

class Catalog {
public:
    explicit Catalog(buffer::BufferPoolManager& buffer_pool);

    Catalog(
        buffer::BufferPoolManager& buffer_pool,
        storage::PageId catalog_page_id
    );

    bool create_table(
        const std::string& table_name,
        const std::vector<ColumnDefinition>& columns,
        storage::PageId first_page_id
    );

    bool drop_table(const std::string& table_name);

    std::optional<TableMetadata> get_table(
        const std::string& table_name
    ) const;

    bool add_index(
        const std::string& table_name,
        const std::string& index_name,
        std::size_t column_index,
        storage::PageId root_page_id
    );

    bool remove_index(
        const std::string& table_name,
        const std::string& index_name
    );

    std::vector<std::string> list_tables() const;

    storage::PageId catalog_page_id() const;

    bool persist();

private:
    bool load_from_page();

    bool serialize_to_buffer(std::byte* buffer, std::size_t size) const;

    bool deserialize_from_buffer(const std::byte* buffer, std::size_t size);

    buffer::BufferPoolManager& buffer_pool_;
    storage::PageId catalog_page_id_;
    bool is_valid_;
    std::unordered_map<std::string, TableMetadata> tables_;
};

}  // namespace forgedb::catalog

