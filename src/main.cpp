#include <iostream>

#include "storage/page.h"

int main() {
    forgedb::storage::Page page;

    page.set_id(1);

    std::cout << "ForgeDB v0.1.0\n";
    std::cout << "Page ID: " << page.id() << '\n';
    std::cout << "Page size: "
              << forgedb::storage::PAGE_SIZE
              << " bytes\n";
    std::cout << "Dirty: "
              << std::boolalpha
              << page.is_dirty()
              << '\n';

    return 0;
}