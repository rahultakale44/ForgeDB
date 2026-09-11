#include <cstddef>
#include <iostream>

#include "storage/disk_manager.h"
#include "storage/page.h"

int main() {
    using forgedb::storage::DiskManager;
    using forgedb::storage::Page;
    using forgedb::storage::PAGE_SIZE;

    const char* database_file = "forgedb.db";

    DiskManager disk_manager(database_file);

    Page write_page;
    write_page.set_id(0);

    const char message[] = "ForgeDB persistent storage";

    for (std::size_t i = 0; message[i] != '\0'; ++i) {
        write_page.data()[i] =
            static_cast<std::byte>(message[i]);
    }

    const bool write_success =
        disk_manager.write_page(0, write_page);

    Page read_page;
    const bool read_success =
        disk_manager.read_page(0, read_page);

    std::cout << "ForgeDB v0.1.0\n";
    std::cout << "Page size: " << PAGE_SIZE << " bytes\n";
    std::cout << "Database file: "
              << disk_manager.file_path() << '\n';
    std::cout << "Write: "
              << (write_success ? "SUCCESS" : "FAILED")
              << '\n';
    std::cout << "Read: "
              << (read_success ? "SUCCESS" : "FAILED")
              << '\n';

    if (read_success) {
        std::cout << "Recovered data: ";

        for (std::size_t i = 0; i < sizeof(message) - 1; ++i) {
            std::cout << static_cast<char>(read_page.data()[i]);
        }

        std::cout << '\n';
    }

    std::cout << "Database size: "
              << disk_manager.file_size()
              << " bytes\n";

    return (write_success && read_success) ? 0 : 1;
}