#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace forgedb::storage {

constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::uint32_t PAGE_MAGIC = 0x46444250;  // "FDBP"
constexpr std::uint16_t PAGE_VERSION = 1;

using PageId = std::uint32_t;

struct PageHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
    PageId page_id;
    std::uint32_t checksum;
    std::uint32_t payload_size;
};

class Page {
public:
    Page();

    PageId id() const;
    void set_id(PageId page_id);

    std::byte* data();
    const std::byte* data() const;

    bool is_dirty() const;
    void set_dirty(bool dirty);

    std::uint32_t checksum() const;

    bool serialize(std::array<std::byte, PAGE_SIZE>& buffer) const;
    bool deserialize(const std::array<std::byte, PAGE_SIZE>& buffer);

private:
    static constexpr std::size_t HEADER_SIZE = sizeof(PageHeader);

    PageId page_id_;
    bool dirty_;
    std::array<std::byte, PAGE_SIZE - HEADER_SIZE> data_;

    static std::uint32_t calculate_checksum(
        const std::byte* data,
        std::size_t size
    );
};

}  // namespace forgedb::storage