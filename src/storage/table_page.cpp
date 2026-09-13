#include "storage/table_page.h"

#include <cstring>
#include <limits>

namespace forgedb::storage {

TablePage::TablePage(Page& page)
    : page_(page) {}

void TablePage::init() {
    set_slot_count(0);
    set_free_space_offset(
        static_cast<std::uint16_t>(
            PAGE_SIZE - sizeof(PageHeader)
        )
    );
}

bool TablePage::insert_tuple(const Tuple& tuple, SlotId& slot_id) {
    const std::size_t tuple_size = tuple.serialized_size();

    if (tuple_size > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    const std::size_t required_space =
        tuple_size + sizeof(SlotDirectory);

    if (available_space() < required_space) {
        return false;
    }

    const std::uint16_t slot_count = get_slot_count();
    const std::uint16_t free_offset = get_free_space_offset();

    const std::uint16_t new_free_offset =
        free_offset - static_cast<std::uint16_t>(tuple_size);

    std::byte* data = page_.data();
    std::byte* tuple_location = data + new_free_offset;

    if (!tuple.serialize(
            tuple_location,
            tuple_size)) {
        return false;
    }

    SlotDirectory new_slot{};
    new_slot.offset = new_free_offset;
    new_slot.size = static_cast<std::uint16_t>(tuple_size);
    new_slot.is_deleted = false;

    slot_id = slot_count;

    write_slot(slot_id, new_slot);

    set_slot_count(slot_count + 1);
    set_free_space_offset(new_free_offset);

    return true;
}

bool TablePage::get_tuple(
    SlotId slot_id,
    Tuple& tuple,
    const Schema& schema
) const {
    SlotDirectory slot{};

    if (!read_slot(slot_id, slot)) {
        return false;
    }

    if (slot.is_deleted) {
        return false;
    }

    const std::byte* data = page_.data();
    const std::byte* tuple_location = data + slot.offset;

    return tuple.deserialize(tuple_location, slot.size, schema);
}

bool TablePage::delete_tuple(SlotId slot_id) {
    SlotDirectory slot{};

    if (!read_slot(slot_id, slot)) {
        return false;
    }

    if (slot.is_deleted) {
        return false;
    }

    slot.is_deleted = true;

    write_slot(slot_id, slot);

    return true;
}

bool TablePage::update_tuple(SlotId slot_id, const Tuple& tuple) {
    SlotDirectory slot{};

    if (!read_slot(slot_id, slot)) {
        return false;
    }

    if (slot.is_deleted) {
        return false;
    }

    const std::size_t new_size = tuple.serialized_size();

    if (new_size > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    if (new_size <= slot.size) {
        std::byte* data = page_.data();
        std::byte* tuple_location = data + slot.offset;

        if (!tuple.serialize(tuple_location, slot.size)) {
            return false;
        }

        slot.size = static_cast<std::uint16_t>(new_size);

        write_slot(slot_id, slot);

        return true;
    }

    if (!delete_tuple(slot_id)) {
        return false;
    }

    SlotId new_slot_id = 0;

    if (!insert_tuple(tuple, new_slot_id)) {
        slot.is_deleted = false;
        write_slot(slot_id, slot);
        return false;
    }

    return true;
}

std::size_t TablePage::tuple_count() const {
    const std::uint16_t slot_count = get_slot_count();

    std::size_t count = 0;

    for (std::uint16_t i = 0; i < slot_count; ++i) {
        SlotDirectory slot{};

        if (read_slot(i, slot) && !slot.is_deleted) {
            ++count;
        }
    }

    return count;
}

std::size_t TablePage::free_space() const {
    return available_space();
}

bool TablePage::is_slot_valid(SlotId slot_id) const {
    if (slot_id >= get_slot_count()) {
        return false;
    }

    SlotDirectory slot{};

    if (!read_slot(slot_id, slot)) {
        return false;
    }

    return !slot.is_deleted;
}

std::uint16_t TablePage::get_slot_count() const {
    std::uint16_t count = 0;

    const std::byte* data = page_.data();

    std::memcpy(&count, data, sizeof(std::uint16_t));

    return count;
}

void TablePage::set_slot_count(std::uint16_t count) {
    std::byte* data = page_.data();

    std::memcpy(data, &count, sizeof(std::uint16_t));
}

std::uint16_t TablePage::get_free_space_offset() const {
    std::uint16_t offset = 0;

    const std::byte* data = page_.data();

    std::memcpy(
        &offset,
        data + sizeof(std::uint16_t),
        sizeof(std::uint16_t)
    );

    return offset;
}

void TablePage::set_free_space_offset(std::uint16_t offset) {
    std::byte* data = page_.data();

    std::memcpy(
        data + sizeof(std::uint16_t),
        &offset,
        sizeof(std::uint16_t)
    );
}

bool TablePage::read_slot(
    SlotId slot_id,
    SlotDirectory& slot
) const {
    const std::uint16_t slot_count = get_slot_count();

    if (slot_id >= slot_count) {
        return false;
    }

    const std::size_t slot_offset =
        HEADER_SIZE + slot_id * sizeof(SlotDirectory);

    const std::byte* data = page_.data();

    std::uint16_t offset = 0;
    std::uint16_t size = 0;
    std::uint8_t deleted_byte = 0;

    std::memcpy(&offset, data + slot_offset, sizeof(std::uint16_t));
    std::memcpy(
        &size,
        data + slot_offset + sizeof(std::uint16_t),
        sizeof(std::uint16_t)
    );
    std::memcpy(
        &deleted_byte,
        data + slot_offset + 2 * sizeof(std::uint16_t),
        sizeof(std::uint8_t)
    );

    slot.offset = offset;
    slot.size = size;
    slot.is_deleted = (deleted_byte != 0);

    return true;
}

void TablePage::write_slot(
    SlotId slot_id,
    const SlotDirectory& slot
) {
    const std::size_t slot_offset =
        HEADER_SIZE + slot_id * sizeof(SlotDirectory);

    std::byte* data = page_.data();

    const std::uint8_t deleted_byte = slot.is_deleted ? 1 : 0;

    std::memcpy(data + slot_offset, &slot.offset, sizeof(std::uint16_t));
    std::memcpy(
        data + slot_offset + sizeof(std::uint16_t),
        &slot.size,
        sizeof(std::uint16_t)
    );
    std::memcpy(
        data + slot_offset + 2 * sizeof(std::uint16_t),
        &deleted_byte,
        sizeof(std::uint8_t)
    );
}

std::size_t TablePage::available_space() const {
    const std::uint16_t slot_count = get_slot_count();
    const std::uint16_t free_offset = get_free_space_offset();

    const std::size_t slot_directory_end =
        HEADER_SIZE + slot_count * sizeof(SlotDirectory) +
        sizeof(SlotDirectory);

    if (free_offset < slot_directory_end) {
        return 0;
    }

    return free_offset - slot_directory_end;
}

}  // namespace forgedb::storage
