#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "indexing/b_plus_tree.h"
#include "storage/page.h"
#include "storage/table_page.h"
#include "storage/tuple.h"

namespace forgedb::storage {

struct RecordId {
    PageId page_id;
    SlotId slot_id;

    bool operator==(const RecordId& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }

    bool operator!=(const RecordId& other) const {
        return !(*this == other);
    }

    RID to_rid() const {
        return (static_cast<RID>(page_id) << 16) |
               static_cast<RID>(slot_id);
    }

    static RecordId from_rid(RID rid) {
        return RecordId{
            static_cast<PageId>(rid >> 16),
            static_cast<SlotId>(rid & 0xFFFF)
        };
    }
};

class TableHeap {
public:
    TableHeap(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        const Schema& schema
    );

    TableHeap(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        const Schema& schema,
        PageId first_page_id
    );

    bool insert_tuple(const Tuple& tuple, RecordId& rid);

    bool get_tuple(const RecordId& rid, Tuple& tuple) const;

    bool delete_tuple(const RecordId& rid);

    bool update_tuple(const RecordId& rid, const Tuple& tuple);

    PageId first_page_id() const;

    std::size_t tuple_count() const;

private:
    PageId find_page_with_space(std::size_t required_space);

    PageId allocate_new_page();
    
    void discover_all_pages();

    forgedb::buffer::BufferPoolManager& buffer_pool_;
    Schema schema_;
    PageId first_page_id_;
    std::vector<PageId> page_ids_;
    bool pages_discovered_;
};

class IndexedTable {
public:
    IndexedTable(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        const Schema& schema,
        std::size_t key_column_index
    );

    IndexedTable(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        const Schema& schema,
        std::size_t key_column_index,
        PageId heap_first_page_id,
        PageId index_root_page_id
    );

    bool insert(const Tuple& tuple);

    bool search_by_key(std::int32_t key, Tuple& tuple);

    bool delete_by_key(std::int32_t key);

    bool update_by_key(std::int32_t key, const Tuple& new_tuple);

    PageId heap_first_page_id() const;
    PageId index_root_page_id() const;

    std::size_t tuple_count() const;

private:
    std::optional<std::int32_t> extract_key(const Tuple& tuple) const;

    forgedb::buffer::BufferPoolManager& buffer_pool_;
    Schema schema_;
    std::size_t key_column_index_;
    std::unique_ptr<TableHeap> table_heap_;
    std::unique_ptr<forgedb::indexing::BPlusTree> index_;
};

}  // namespace forgedb::storage
