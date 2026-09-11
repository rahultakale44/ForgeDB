#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "storage/disk_manager.h"
#include "storage/page.h"

namespace forgedb::buffer {

class BufferPoolManager {
public:
    BufferPoolManager(
        std::size_t pool_size,
        forgedb::storage::DiskManager& disk_manager
    );

    forgedb::storage::Page* fetch_page(
        forgedb::storage::PageId page_id
    );

    bool unpin_page(
        forgedb::storage::PageId page_id,
        bool is_dirty
    );

    bool flush_page(
        forgedb::storage::PageId page_id
    );

    void flush_all_pages();

    std::size_t pool_size() const;

    std::size_t pinned_page_count() const;

private:
    std::size_t find_free_frame() const;
    std::size_t find_victim_frame();

    std::size_t pool_size_;
    forgedb::storage::DiskManager& disk_manager_;

    std::vector<forgedb::storage::Page> pages_;
    std::vector<std::size_t> pin_counts_;
    std::vector<bool> valid_;

    std::unordered_map<
        forgedb::storage::PageId,
        std::size_t
    > page_table_;

    std::size_t next_victim_;
};

}  // namespace forgedb::buffer