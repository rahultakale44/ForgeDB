#include "storage/disk_manager.h"

#include <filesystem>

namespace forgedb::storage {

DiskManager::DiskManager(const std::string& file_path)
    : file_path_(file_path) {
    file_.open(
        file_path_,
        std::ios::in | std::ios::out | std::ios::binary
    );

    if (!file_.is_open()) {
        std::ofstream create_file(file_path_, std::ios::binary);
        create_file.close();

        file_.open(
            file_path_,
            std::ios::in | std::ios::out | std::ios::binary
        );
    }
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool DiskManager::read_page(PageId page_id, Page& page) {
    if (!file_.is_open()) {
        return false;
    }

    const std::streamoff offset =
        static_cast<std::streamoff>(page_id) *
        static_cast<std::streamoff>(PAGE_SIZE);

    file_.clear();
    file_.seekg(offset, std::ios::beg);

    if (!file_) {
        return false;
    }

    file_.read(
        reinterpret_cast<char*>(page.data()),
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    if (file_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        file_.clear();
        return false;
    }

    page.set_id(page_id);
    page.set_dirty(false);

    return true;
}

bool DiskManager::write_page(
    PageId page_id,
    const Page& page
) {
    if (!file_.is_open()) {
        return false;
    }

    const std::streamoff offset =
        static_cast<std::streamoff>(page_id) *
        static_cast<std::streamoff>(PAGE_SIZE);

    file_.clear();
    file_.seekp(offset, std::ios::beg);

    if (!file_) {
        return false;
    }

    file_.write(
        reinterpret_cast<const char*>(page.data()),
        static_cast<std::streamsize>(PAGE_SIZE)
    );

    file_.flush();

    return static_cast<bool>(file_);
}

std::uint64_t DiskManager::file_size() const {
    if (!file_.is_open()) {
        return 0;
    }

    file_.clear();
    file_.seekg(0, std::ios::end);

    const auto size = file_.tellg();

    return size < 0
        ? 0
        : static_cast<std::uint64_t>(size);
}

const std::string& DiskManager::file_path() const {
    return file_path_;
}

}  // namespace forgedb::storage