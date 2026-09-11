#include "storage/page.h"

#include <algorithm>
#include <cstring>

namespace forgedb::storage {

Page::Page()
    : page_id_(0),
      dirty_(false),
      data_{} {
    data_.fill(std::byte{0});
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

std::uint32_t Page::checksum() const {
    return calculate_checksum(
        data_.data(),
        data_.size()
    );
}

std::uint32_t Page::calculate_checksum(
    const std::byte* data,
    std::size_t size
) {
    std::uint32_t hash = 2166136261u;

    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint8_t>(data[i]);
        hash *= 16777619u;
    }

    return hash;
}

bool Page::serialize(
    std::array<std::byte, PAGE_SIZE>& buffer
) const {
    buffer.fill(std::byte{0});

    PageHeader header{
        PAGE_MAGIC,
        PAGE_VERSION,
        static_cast<std::uint16_t>(dirty_ ? 1 : 0),
        page_id_,
        checksum(),
        static_cast<std::uint32_t>(data_.size())
    };

    std::memcpy(
        buffer.data(),
        &header,
        sizeof(PageHeader)
    );

    std::memcpy(
        buffer.data() + HEADER_SIZE,
        data_.data(),
        data_.size()
    );

    return true;
}

bool Page::deserialize(
    const std::array<std::byte, PAGE_SIZE>& buffer
) {
    PageHeader header{};

    std::memcpy(
        &header,
        buffer.data(),
        sizeof(PageHeader)
    );

    if (header.magic != PAGE_MAGIC) {
        return false;
    }

    if (header.version != PAGE_VERSION) {
        return false;
    }

    if (header.payload_size != data_.size()) {
        return false;
    }

    const auto calculated_checksum =
        calculate_checksum(
            buffer.data() + HEADER_SIZE,
            data_.size()
        );

    if (calculated_checksum != header.checksum) {
        return false;
    }

    page_id_ = header.page_id;
    dirty_ = (header.flags & 1u) != 0;

    std::memcpy(
        data_.data(),
        buffer.data() + HEADER_SIZE,
        data_.size()
    );

    return true;
}

}  // namespace forgedb::storage