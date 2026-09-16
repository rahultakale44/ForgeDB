#include "catalog/catalog.h"

#include <cstring>

namespace forgedb::catalog {

storage::Schema TableMetadata::to_schema() const {
    storage::Schema schema;

    for (const auto& col : columns) {
        schema.add_column(col.type, col.max_length);
    }

    return schema;
}

Catalog::Catalog(buffer::BufferPoolManager& buffer_pool)
    : buffer_pool_(buffer_pool),
      catalog_page_id_(0),
      is_valid_(false),
      tables_() {
    storage::Page* page = buffer_pool_.new_page(catalog_page_id_);

    if (page == nullptr) {
        return;
    }

    is_valid_ = true;

    std::memset(page->data(), 0, storage::PAGE_DATA_SIZE);

    buffer_pool_.unpin_page(catalog_page_id_, true);
}

Catalog::Catalog(
    buffer::BufferPoolManager& buffer_pool,
    storage::PageId catalog_page_id
)
    : buffer_pool_(buffer_pool),
      catalog_page_id_(catalog_page_id),
      is_valid_(true),
      tables_() {
    load_from_page();
}

bool Catalog::create_table(
    const std::string& table_name,
    const std::vector<ColumnDefinition>& columns,
    storage::PageId first_page_id
) {
    if (tables_.find(table_name) != tables_.end()) {
        return false;
    }

    TableMetadata metadata(table_name, columns, first_page_id);
    tables_[table_name] = metadata;

    return persist();
}

bool Catalog::drop_table(const std::string& table_name) {
    const auto it = tables_.find(table_name);

    if (it == tables_.end()) {
        return false;
    }

    tables_.erase(it);

    return persist();
}

std::optional<TableMetadata> Catalog::get_table(
    const std::string& table_name
) const {
    const auto it = tables_.find(table_name);

    if (it == tables_.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool Catalog::add_index(
    const std::string& table_name,
    const std::string& index_name,
    std::size_t column_index,
    storage::PageId root_page_id
) {
    auto it = tables_.find(table_name);

    if (it == tables_.end()) {
        return false;
    }

    for (const auto& idx : it->second.indexes) {
        if (idx.index_name == index_name) {
            return false;
        }
    }

    it->second.indexes.emplace_back(index_name, column_index, root_page_id);

    return persist();
}

bool Catalog::remove_index(
    const std::string& table_name,
    const std::string& index_name
) {
    auto it = tables_.find(table_name);

    if (it == tables_.end()) {
        return false;
    }

    auto& indexes = it->second.indexes;
    auto idx_it = indexes.begin();

    while (idx_it != indexes.end()) {
        if (idx_it->index_name == index_name) {
            indexes.erase(idx_it);
            return persist();
        }
        ++idx_it;
    }

    return false;
}

std::vector<std::string> Catalog::list_tables() const {
    std::vector<std::string> table_names;
    table_names.reserve(tables_.size());

    for (const auto& [name, _] : tables_) {
        table_names.push_back(name);
    }

    return table_names;
}

storage::PageId Catalog::catalog_page_id() const {
    return catalog_page_id_;
}

bool Catalog::persist() {
    if (!is_valid_) {
        return false;
    }

    storage::Page* page = buffer_pool_.fetch_page(catalog_page_id_);

    if (page == nullptr) {
        return false;
    }

    const bool success =
        serialize_to_buffer(page->data(), storage::PAGE_DATA_SIZE);

    buffer_pool_.unpin_page(catalog_page_id_, success);

    return success;
}

bool Catalog::load_from_page() {
    if (!is_valid_) {
        return false;
    }

    storage::Page* page = buffer_pool_.fetch_page(catalog_page_id_);

    if (page == nullptr) {
        return false;
    }

    const bool success =
        deserialize_from_buffer(page->data(), storage::PAGE_DATA_SIZE);

    buffer_pool_.unpin_page(catalog_page_id_, false);

    return success;
}

bool Catalog::serialize_to_buffer(
    std::byte* buffer,
    std::size_t size
) const {
    std::size_t offset = 0;

    const std::uint32_t table_count =
        static_cast<std::uint32_t>(tables_.size());

    if (offset + sizeof(std::uint32_t) > size) {
        return false;
    }

    std::memcpy(buffer + offset, &table_count, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    for (const auto& [name, metadata] : tables_) {
        const std::uint32_t name_len =
            static_cast<std::uint32_t>(metadata.table_name.size());

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(buffer + offset, &name_len, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        if (offset + name_len > size) {
            return false;
        }

        std::memcpy(
            buffer + offset,
            metadata.table_name.data(),
            name_len
        );
        offset += name_len;

        const std::uint32_t column_count =
            static_cast<std::uint32_t>(metadata.columns.size());

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(buffer + offset, &column_count, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        for (const auto& col : metadata.columns) {
            const std::uint32_t col_name_len =
                static_cast<std::uint32_t>(col.name.size());

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(
                buffer + offset,
                &col_name_len,
                sizeof(std::uint32_t)
            );
            offset += sizeof(std::uint32_t);

            if (offset + col_name_len > size) {
                return false;
            }

            std::memcpy(buffer + offset, col.name.data(), col_name_len);
            offset += col_name_len;

            const std::uint8_t type_byte =
                static_cast<std::uint8_t>(col.type);

            if (offset + sizeof(std::uint8_t) > size) {
                return false;
            }

            std::memcpy(buffer + offset, &type_byte, sizeof(std::uint8_t));
            offset += sizeof(std::uint8_t);

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            const std::uint32_t max_len =
                static_cast<std::uint32_t>(col.max_length);

            std::memcpy(buffer + offset, &max_len, sizeof(std::uint32_t));
            offset += sizeof(std::uint32_t);

            const std::uint8_t nullable_byte = col.nullable ? 1 : 0;

            if (offset + sizeof(std::uint8_t) > size) {
                return false;
            }

            std::memcpy(
                buffer + offset,
                &nullable_byte,
                sizeof(std::uint8_t)
            );
            offset += sizeof(std::uint8_t);
        }

        if (offset + sizeof(storage::PageId) > size) {
            return false;
        }

        std::memcpy(
            buffer + offset,
            &metadata.first_page_id,
            sizeof(storage::PageId)
        );
        offset += sizeof(storage::PageId);

        const std::uint32_t index_count =
            static_cast<std::uint32_t>(metadata.indexes.size());

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(buffer + offset, &index_count, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        for (const auto& idx : metadata.indexes) {
            const std::uint32_t idx_name_len =
                static_cast<std::uint32_t>(idx.index_name.size());

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(
                buffer + offset,
                &idx_name_len,
                sizeof(std::uint32_t)
            );
            offset += sizeof(std::uint32_t);

            if (offset + idx_name_len > size) {
                return false;
            }

            std::memcpy(
                buffer + offset,
                idx.index_name.data(),
                idx_name_len
            );
            offset += idx_name_len;

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            const std::uint32_t col_idx =
                static_cast<std::uint32_t>(idx.column_index);

            std::memcpy(buffer + offset, &col_idx, sizeof(std::uint32_t));
            offset += sizeof(std::uint32_t);

            if (offset + sizeof(storage::PageId) > size) {
                return false;
            }

            std::memcpy(
                buffer + offset,
                &idx.root_page_id,
                sizeof(storage::PageId)
            );
            offset += sizeof(storage::PageId);
        }
    }

    return true;
}

bool Catalog::deserialize_from_buffer(
    const std::byte* buffer,
    std::size_t size
) {
    std::size_t offset = 0;
    std::uint32_t table_count = 0;

    if (offset + sizeof(std::uint32_t) > size) {
        return false;
    }

    std::memcpy(&table_count, buffer + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    if (table_count == 0) {
        return true;
    }

    tables_.clear();

    for (std::uint32_t t = 0; t < table_count; ++t) {
        TableMetadata metadata;

        std::uint32_t name_len = 0;

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(&name_len, buffer + offset, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        if (offset + name_len > size) {
            return false;
        }

        metadata.table_name = std::string(name_len, '\0');
        std::memcpy(metadata.table_name.data(), buffer + offset, name_len);
        offset += name_len;

        std::uint32_t column_count = 0;

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(&column_count, buffer + offset, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        for (std::uint32_t c = 0; c < column_count; ++c) {
            std::uint32_t col_name_len = 0;

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(
                &col_name_len,
                buffer + offset,
                sizeof(std::uint32_t)
            );
            offset += sizeof(std::uint32_t);

            if (offset + col_name_len > size) {
                return false;
            }

            std::string col_name(col_name_len, '\0');
            std::memcpy(col_name.data(), buffer + offset, col_name_len);
            offset += col_name_len;

            std::uint8_t type_byte = 0;

            if (offset + sizeof(std::uint8_t) > size) {
                return false;
            }

            std::memcpy(&type_byte, buffer + offset, sizeof(std::uint8_t));
            offset += sizeof(std::uint8_t);

            const storage::ValueType type =
                static_cast<storage::ValueType>(type_byte);

            std::uint32_t max_len = 0;

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(&max_len, buffer + offset, sizeof(std::uint32_t));
            offset += sizeof(std::uint32_t);

            std::uint8_t nullable_byte = 0;

            if (offset + sizeof(std::uint8_t) > size) {
                return false;
            }

            std::memcpy(
                &nullable_byte,
                buffer + offset,
                sizeof(std::uint8_t)
            );
            offset += sizeof(std::uint8_t);

            const bool nullable = (nullable_byte != 0);

            metadata.columns.emplace_back(
                col_name,
                type,
                max_len,
                nullable
            );
        }

        if (offset + sizeof(storage::PageId) > size) {
            return false;
        }

        std::memcpy(
            &metadata.first_page_id,
            buffer + offset,
            sizeof(storage::PageId)
        );
        offset += sizeof(storage::PageId);

        std::uint32_t index_count = 0;

        if (offset + sizeof(std::uint32_t) > size) {
            return false;
        }

        std::memcpy(&index_count, buffer + offset, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);

        for (std::uint32_t i = 0; i < index_count; ++i) {
            std::uint32_t idx_name_len = 0;

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(
                &idx_name_len,
                buffer + offset,
                sizeof(std::uint32_t)
            );
            offset += sizeof(std::uint32_t);

            if (offset + idx_name_len > size) {
                return false;
            }

            std::string idx_name(idx_name_len, '\0');
            std::memcpy(idx_name.data(), buffer + offset, idx_name_len);
            offset += idx_name_len;

            std::uint32_t col_idx = 0;

            if (offset + sizeof(std::uint32_t) > size) {
                return false;
            }

            std::memcpy(&col_idx, buffer + offset, sizeof(std::uint32_t));
            offset += sizeof(std::uint32_t);

            storage::PageId root_page_id = 0;

            if (offset + sizeof(storage::PageId) > size) {
                return false;
            }

            std::memcpy(
                &root_page_id,
                buffer + offset,
                sizeof(storage::PageId)
            );
            offset += sizeof(storage::PageId);

            metadata.indexes.emplace_back(idx_name, col_idx, root_page_id);
        }

        tables_[metadata.table_name] = metadata;
    }

    return true;
}

}  // namespace forgedb::catalog

