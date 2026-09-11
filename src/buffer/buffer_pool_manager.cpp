#include "buffer/buffer_pool_manager.h"

#include <limits>

namespace forgedb::buffer {

BufferPoolManager::BufferPoolManager(
    std::size_t pool_size,
    forgedb::storage::DiskManager& disk_manager
)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      pages_(pool_size),
      pin_counts_(pool_size, 0),
      valid_(pool_size, false),
      page_table_(),
      next_victim_(0) {}

forgedb::storage::Page* BufferPoolManager::new_page(
    forgedb::storage::PageId& page_id
) {
    const auto allocated_page_id =
        disk_manager_.allocate_page();

    if (allocated_page_id ==
        std::numeric_limits<forgedb::storage::PageId>::max()) {
        return nullptr;
    }

    std::size_t frame_id = find_free_frame();

    if (frame_id == pool_size_) {
        frame_id = find_victim_frame();

        if (frame_id == pool_size_) {
            return nullptr;
        }

        const auto old_page_id =
            pages_[frame_id].id();

        if (pages_[frame_id].is_dirty()) {
            if (!disk_manager_.write_page(
                    old_page_id,
                    pages_[frame_id])) {
                return nullptr;
            }
        }

        page_table_.erase(old_page_id);
    }

    pages_[frame_id] = forgedb::storage::Page{};

    pages_[frame_id].set_id(
        allocated_page_id
    );

    pages_[frame_id].set_dirty(false);

    page_table_[allocated_page_id] = frame_id;

    valid_[frame_id] = true;
    pin_counts_[frame_id] = 1;

    page_id = allocated_page_id;

    return &pages_[frame_id];
}

forgedb::storage::Page* BufferPoolManager::fetch_page(
    forgedb::storage::PageId page_id
) {
    const auto existing = page_table_.find(page_id);

    if (existing != page_table_.end()) {
        const std::size_t frame_id = existing->second;

        ++pin_counts_[frame_id];

        return &pages_[frame_id];
    }

    std::size_t frame_id = find_free_frame();

    if (frame_id == pool_size_) {
        frame_id = find_victim_frame();

        if (frame_id == pool_size_) {
            return nullptr;
        }

        const auto old_page_id =
            pages_[frame_id].id();

        if (pages_[frame_id].is_dirty()) {
            if (!disk_manager_.write_page(
                    old_page_id,
                    pages_[frame_id])) {
                return nullptr;
            }
        }

        page_table_.erase(old_page_id);
    }

    if (!disk_manager_.read_page(
            page_id,
            pages_[frame_id])) {
        return nullptr;
    }

    page_table_[page_id] = frame_id;

    valid_[frame_id] = true;
    pin_counts_[frame_id] = 1;

    return &pages_[frame_id];
}

bool BufferPoolManager::unpin_page(
    forgedb::storage::PageId page_id,
    bool is_dirty
) {
    const auto it = page_table_.find(page_id);

    if (it == page_table_.end()) {
        return false;
    }

    const std::size_t frame_id = it->second;

    if (pin_counts_[frame_id] == 0) {
        return false;
    }

    --pin_counts_[frame_id];

    if (is_dirty) {
        pages_[frame_id].set_dirty(true);
    }

    return true;
}

bool BufferPoolManager::flush_page(
    forgedb::storage::PageId page_id
) {
    const auto it = page_table_.find(page_id);

    if (it == page_table_.end()) {
        return false;
    }

    const std::size_t frame_id = it->second;

    if (!disk_manager_.write_page(
            page_id,
            pages_[frame_id])) {
        return false;
    }

    pages_[frame_id].set_dirty(false);

    return true;
}

void BufferPoolManager::flush_all_pages() {
    for (const auto& entry : page_table_) {
        const forgedb::storage::PageId page_id =
            entry.first;

        flush_page(page_id);
    }
}

std::size_t BufferPoolManager::pool_size() const {
    return pool_size_;
}

std::size_t BufferPoolManager::pinned_page_count() const {
    std::size_t count = 0;

    for (const std::size_t pin_count : pin_counts_) {
        if (pin_count > 0) {
            ++count;
        }
    }

    return count;
}

std::size_t BufferPoolManager::find_free_frame() const {
    for (std::size_t i = 0; i < pool_size_; ++i) {
        if (!valid_[i]) {
            return i;
        }
    }

    return pool_size_;
}

std::size_t BufferPoolManager::find_victim_frame() {
    if (pool_size_ == 0) {
        return pool_size_;
    }

    for (std::size_t i = 0; i < pool_size_; ++i) {
        const std::size_t frame_id =
            (next_victim_ + i) % pool_size_;

        if (valid_[frame_id] &&
            pin_counts_[frame_id] == 0) {

            next_victim_ =
                (frame_id + 1) % pool_size_;

            return frame_id;
        }
    }

    return pool_size_;
}

}  // namespace forgedb::buffer