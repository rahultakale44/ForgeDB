#include "storage/page.h"

#include <algorithm>

namespace forgedb::storage {

Page::Page()
    : page_id_(0),
      dirty_(false),
      data_{} {
    std::fill(data_.begin(), data_.end(), std::byte{0});
}

PageId Page::id() const {
    return page_id_;
}

void Page::set_id(PageId page_id) {
    page_id_ = page_id;
}

std::byte* Page::data() {
    return data_.data();
}

const std::byte* Page::data() const {
    return data_.data();
}

bool Page::is_dirty() const {
    return dirty_;
}

void Page::set_dirty(bool dirty) {
    dirty_ = dirty;
}

}  // namespace forgedb::storage