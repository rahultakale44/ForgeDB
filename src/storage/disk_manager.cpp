#include "storage/disk_manager.h"

#include <array>
#include <cstdint>

namespace forgedb::storage {

DiskManager::DiskManager(const std::string& file_path)
    : file_path_(file_path),
      file_() {
    file_.open(
        file_path_,
        std::ios::in |
        std::ios::out |
        std::ios::binary
    );

    if (!file_.is_open()) {
        file_.clear();

        file_.open(
            file_path_,
            std::ios::out |
            std::ios::binary
        );

        file_.close();

        file_.open(
            file_path_,
            std::ios::in |
            std::ios::out |
            std::ios::binary
        );
    }
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool DiskManager::read_page(
    PageId page_id,
    Page& page
) {
    if (!file_.is_open()) {
        return false;
    }

    const std::uint64_t offset =
        static_cast<std::uint64_t>(page_id) * PAGE_SIZE;

    file_.clear();
    file_.seekg(
        static_cast<std::streamoff>(offset),
        std::ios::beg
    );

    if (!file_) {
        return false;
    }

    std::array<std::byte, PAGE_SIZE> buffer{};

    file_.read(
        reinterpret_cast<char*>(buffer.data()),
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    if (file_.gcount() !=
        static_cast<std::streamsize>(PAGE_SIZE)) {
        file_.clear();
        return false;
    }

    if (!page.deserialize(buffer)) {
        return false;
    }

    return true;
}

bool DiskManager::write_page(
    PageId page_id,
    const Page& page
) {
    if (!file_.is_open()) {
        return false;
    }

    std::array<std::byte, PAGE_SIZE> buffer{};

    if (!page.serialize(buffer)) {
        return false;
    }

    const std::uint64_t offset =
        static_cast<std::uint64_t>(page_id) * PAGE_SIZE;

    file_.clear();
    file_.seekp(
        static_cast<std::streamoff>(offset),
        std::ios::beg
    );

    if (!file_) {
        return false;
    }

    file_.write(
        reinterpret_cast<const char*>(buffer.data()),
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    if (!file_) {
        return false;
    }

    file_.flush();

    return static_cast<bool>(file_);
}

std::uint64_t DiskManager::file_size() const {
    if (!file_.is_open()) {
        return 0;
    }

    file_.clear();

    const auto current_position = file_.tellg();

    file_.seekg(0, std::ios::end);

    const auto size = file_.tellg();

    file_.seekg(current_position);

    if (size < 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(size);
}

const std::string& DiskManager::file_path() const {
    return file_path_;
}

}  // namespace forgedb::storage