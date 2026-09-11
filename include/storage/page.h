#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace forgedb::storage {

constexpr std::size_t PAGE_SIZE = 4096;

using PageId = std::uint32_t;

class Page {
public:
    Page();

    PageId id() const;
    void set_id(PageId page_id);

    std::byte* data();
    const std::byte* data() const;

    bool is_dirty() const;
    void set_dirty(bool dirty);

private:
    PageId page_id_;
    bool dirty_;
    std::array<std::byte, PAGE_SIZE> data_;
};

}  // namespace forgedb::storage