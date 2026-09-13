#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "storage/page.h"
#include "storage/tuple.h"

namespace forgedb::storage {

using SlotId = std::uint16_t;

struct SlotDirectory {
    std::uint16_t offset;
    std::uint16_t size;
    bool is_deleted;
};

class TablePage {
public:
    explicit TablePage(Page& page);

    bool insert_tuple(const Tuple& tuple, SlotId& slot_id);

    bool get_tuple(SlotId slot_id, Tuple& tuple, const Schema& schema) const;

    bool delete_tuple(SlotId slot_id);

    bool update_tuple(SlotId slot_id, const Tuple& tuple);

    std::size_t tuple_count() const;

    std::size_t free_space() const;

    bool is_slot_valid(SlotId slot_id) const;

    void init();

private:
    static constexpr std::size_t HEADER_SIZE = sizeof(std::uint16_t) * 2;

    std::uint16_t get_slot_count() const;
    void set_slot_count(std::uint16_t count);

    std::uint16_t get_free_space_offset() const;
    void set_free_space_offset(std::uint16_t offset);

    bool read_slot(SlotId slot_id, SlotDirectory& slot) const;
    void write_slot(SlotId slot_id, const SlotDirectory& slot);

    std::size_t available_space() const;

    Page& page_;
};

}  // namespace forgedb::storage
