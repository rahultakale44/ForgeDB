#include "storage/table_heap.h"

#include <cstring>

#include "storage/table_page.h"

namespace forgedb::storage {

TableHeap::TableHeap(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    const Schema& schema
)
    : buffer_pool_(buffer_pool),
      schema_(schema),
      first_page_id_(0),
      page_ids_(),
      pages_discovered_(true) {
    first_page_id_ = allocate_new_page();
}

TableHeap::TableHeap(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    const Schema& schema,
    PageId first_page_id
)
    : buffer_pool_(buffer_pool),
      schema_(schema),
      first_page_id_(first_page_id),
      page_ids_(),
      pages_discovered_(false) {
    // When reopening, we only know the first page ID
    // We don't attempt to discover additional pages here because:
    // 1. We don't have a reliable way to distinguish table pages from index pages
    // 2. Additional pages will be discovered on-demand when tuple_count() is called
    // 3. Existing tuples remain accessible through their RecordIds
    page_ids_.push_back(first_page_id);
}

bool TableHeap::insert_tuple(const Tuple& tuple, RecordId& rid) {
    const std::size_t tuple_size = tuple.serialized_size();

    PageId target_page_id = find_page_with_space(tuple_size);

    if (target_page_id == 0) {
        target_page_id = allocate_new_page();

        if (target_page_id == 0) {
            return false;
        }
    }

    Page* page = buffer_pool_.fetch_page(target_page_id);

    if (page == nullptr) {
        return false;
    }

    TablePage table_page(*page);

    SlotId slot_id = 0;

    if (!table_page.insert_tuple(tuple, slot_id)) {
        buffer_pool_.unpin_page(target_page_id, false);
        return false;
    }

    buffer_pool_.unpin_page(target_page_id, true);

    rid.page_id = target_page_id;
    rid.slot_id = slot_id;

    return true;
}

bool TableHeap::get_tuple(const RecordId& rid, Tuple& tuple) const {
    Page* page = buffer_pool_.fetch_page(rid.page_id);

    if (page == nullptr) {
        return false;
    }

    TablePage table_page(*page);

    const bool success =
        table_page.get_tuple(rid.slot_id, tuple, schema_);

    buffer_pool_.unpin_page(rid.page_id, false);

    return success;
}

bool TableHeap::delete_tuple(const RecordId& rid) {
    Page* page = buffer_pool_.fetch_page(rid.page_id);

    if (page == nullptr) {
        return false;
    }

    TablePage table_page(*page);

    const bool success = table_page.delete_tuple(rid.slot_id);

    buffer_pool_.unpin_page(rid.page_id, success);

    return success;
}

bool TableHeap::update_tuple(
    const RecordId& rid,
    const Tuple& tuple
) {
    Page* page = buffer_pool_.fetch_page(rid.page_id);

    if (page == nullptr) {
        return false;
    }

    TablePage table_page(*page);

    const bool success = table_page.update_tuple(rid.slot_id, tuple);

    buffer_pool_.unpin_page(rid.page_id, success);

    return success;
}

PageId TableHeap::first_page_id() const {
    return first_page_id_;
}

std::size_t TableHeap::tuple_count() const {
    // Ensure all pages are discovered before counting
    const_cast<TableHeap*>(this)->discover_all_pages();
    
    std::size_t count = 0;

    for (PageId page_id : page_ids_) {
        Page* page = buffer_pool_.fetch_page(page_id);

        if (page == nullptr) {
            continue;
        }

        TablePage table_page(*page);

        count += table_page.tuple_count();

        buffer_pool_.unpin_page(page_id, false);
    }

    return count;
}

PageId TableHeap::find_page_with_space(std::size_t required_space) {
    for (PageId page_id : page_ids_) {
        Page* page = buffer_pool_.fetch_page(page_id);

        if (page == nullptr) {
            continue;
        }

        TablePage table_page(*page);

        if (table_page.free_space() >= required_space) {
            buffer_pool_.unpin_page(page_id, false);
            return page_id;
        }

        buffer_pool_.unpin_page(page_id, false);
    }

    return 0;
}

PageId TableHeap::allocate_new_page() {
    PageId page_id = 0;

    Page* page = buffer_pool_.new_page(page_id);

    if (page == nullptr) {
        return 0;
    }

    TablePage table_page(*page);
    table_page.init();

    buffer_pool_.unpin_page(page_id, true);

    page_ids_.push_back(page_id);

    return page_id;
}

void TableHeap::discover_all_pages() {
    if (pages_discovered_) {
        return;
    }
    
    // Discover additional table pages by scanning forward from first_page_id
    // We already have first_page_id in page_ids_, so start with next page
    PageId next_page_id = first_page_id_ + 1;
    
    // Scan up to a reasonable limit to find table pages
    // We need to continue scanning even if we hit non-table pages (like B+ tree nodes)
    // because table pages may not be contiguous
    const PageId max_scan = 100;
    PageId consecutive_missing = 0;
    const PageId max_consecutive_missing = 5;
    
    for (PageId offset = 0; offset < max_scan; ++offset) {
        PageId candidate_page_id = next_page_id + offset;
        
        Page* page = buffer_pool_.fetch_page(candidate_page_id);
        
        if (page == nullptr) {
            // Page doesn't exist
            consecutive_missing++;
            if (consecutive_missing >= max_consecutive_missing) {
                // Stop if we've hit several missing pages in a row
                break;
            }
            continue;
        }
        
        // Reset consecutive missing counter
        consecutive_missing = 0;
        
        // Check if this looks like a valid table page by examining its structure
        std::byte* data = page->data();
        std::uint16_t slot_count = 0;
        std::uint16_t free_offset = 0;
        std::memcpy(&slot_count, data, sizeof(std::uint16_t));
        std::memcpy(&free_offset, data + sizeof(std::uint16_t), sizeof(std::uint16_t));
        
        buffer_pool_.unpin_page(candidate_page_id, false);
        
        // Heuristic: table pages have:
        // - slot_count in reasonable range (0-1000)
        // - free_offset >= slot_directory_size (header + slots)
        // B+ tree nodes have different structure and will fail these checks
        const std::uint16_t header_size = 2 * sizeof(std::uint16_t);
        const std::uint16_t slot_directory_size = header_size + slot_count * sizeof(std::uint32_t);
        
        const bool looks_like_table_page = 
            (slot_count <= 1000) &&
            (free_offset >= slot_directory_size) &&
            (free_offset <= 4096);
        
        if (looks_like_table_page) {
            // Only add if not already in the list
            bool already_added = false;
            for (PageId existing_id : page_ids_) {
                if (existing_id == candidate_page_id) {
                    already_added = true;
                    break;
                }
            }
            
            if (!already_added) {
                page_ids_.push_back(candidate_page_id);
            }
        }
        // Don't break on non-table pages - continue scanning
        // because table pages may not be contiguous (B+ tree pages in between)
    }
    
    pages_discovered_ = true;
}

IndexedTable::IndexedTable(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    const Schema& schema,
    std::size_t key_column_index
)
    : buffer_pool_(buffer_pool),
      schema_(schema),
      key_column_index_(key_column_index) {
    table_heap_ = std::make_unique<TableHeap>(buffer_pool_, schema_);

    index_ = std::make_unique<forgedb::indexing::BPlusTree>(
        buffer_pool_
    );
}

IndexedTable::IndexedTable(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    const Schema& schema,
    std::size_t key_column_index,
    PageId heap_first_page_id,
    PageId index_root_page_id
)
    : buffer_pool_(buffer_pool),
      schema_(schema),
      key_column_index_(key_column_index) {
    table_heap_ = std::make_unique<TableHeap>(
        buffer_pool_,
        schema_,
        heap_first_page_id
    );

    index_ = std::make_unique<forgedb::indexing::BPlusTree>(
        buffer_pool_,
        index_root_page_id
    );
}

bool IndexedTable::insert(const Tuple& tuple) {
    const auto key_opt = extract_key(tuple);

    if (!key_opt.has_value()) {
        return false;
    }

    const std::int32_t key = *key_opt;

    PageId existing_page_id = 0;

    if (index_->search(key, existing_page_id)) {
        return false;
    }

    RecordId rid{};

    if (!table_heap_->insert_tuple(tuple, rid)) {
        return false;
    }

    const RID rid_value = rid.to_rid();

    if (!index_->insert(key, rid_value)) {
        table_heap_->delete_tuple(rid);
        return false;
    }

    return true;
}

bool IndexedTable::search_by_key(std::int32_t key, Tuple& tuple) {
    RID rid_value = 0;

    if (!index_->search(key, rid_value)) {
        return false;
    }

    const RecordId rid = RecordId::from_rid(rid_value);

    return table_heap_->get_tuple(rid, tuple);
}

bool IndexedTable::delete_by_key(std::int32_t key) {
    RID rid_value = 0;

    if (!index_->search(key, rid_value)) {
        return false;
    }

    const RecordId rid = RecordId::from_rid(rid_value);

    if (!table_heap_->delete_tuple(rid)) {
        return false;
    }

    return true;
}

bool IndexedTable::update_by_key(
    std::int32_t key,
    const Tuple& new_tuple
) {
    const auto new_key_opt = extract_key(new_tuple);

    if (!new_key_opt.has_value()) {
        return false;
    }

    if (*new_key_opt != key) {
        return false;
    }

    RID rid_value = 0;

    if (!index_->search(key, rid_value)) {
        return false;
    }

    const RecordId rid = RecordId::from_rid(rid_value);

    return table_heap_->update_tuple(rid, new_tuple);
}

PageId IndexedTable::heap_first_page_id() const {
    return table_heap_->first_page_id();
}

PageId IndexedTable::index_root_page_id() const {
    return index_->root_page_id();
}

std::size_t IndexedTable::tuple_count() const {
    return table_heap_->tuple_count();
}

std::optional<std::int32_t> IndexedTable::extract_key(
    const Tuple& tuple
) const {
    if (key_column_index_ >= tuple.value_count()) {
        return std::nullopt;
    }

    const Value& key_value = tuple.get_value(key_column_index_);

    if (key_value.type() != ValueType::INTEGER) {
        return std::nullopt;
    }

    return key_value.as_int();
}

}  // namespace forgedb::storage
