#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "storage/page.h"

namespace forgedb::storage {

class DiskManager {
public:
    explicit DiskManager(const std::string& file_path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    bool read_page(PageId page_id, Page& page);
    bool write_page(PageId page_id, const Page& page);

    std::uint64_t file_size() const;
    const std::string& file_path() const;

private:
    std::string file_path_;
    mutable std::fstream file_;
};

}  // namespace forgedb::storage