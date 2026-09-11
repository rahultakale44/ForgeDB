#include <array>
#include <cstddef>
#include <iostream>

#include "storage/page.h"

int main() {
    using forgedb::storage::Page;
    using forgedb::storage::PAGE_SIZE;

    Page original;
    original.set_id(42);

    const char message[] = "ForgeDB serialization test";

    for (std::size_t i = 0; message[i] != '\0'; ++i) {
        original.data()[i] =
            static_cast<std::byte>(message[i]);
    }

    std::array<std::byte, PAGE_SIZE> buffer{};

    const bool serialized =
        original.serialize(buffer);

    Page restored;

    const bool deserialized =
        restored.deserialize(buffer);

    std::cout << "ForgeDB v0.1.0\n";
    std::cout << "Page size: "
              << PAGE_SIZE
              << " bytes\n";

    std::cout << "Serialization: "
              << (serialized ? "SUCCESS" : "FAILED")
              << '\n';

    std::cout << "Deserialization: "
              << (deserialized ? "SUCCESS" : "FAILED")
              << '\n';

    std::cout << "Restored Page ID: "
              << restored.id()
              << '\n';

    std::cout << "Restored data: ";

    if (deserialized) {
        for (std::size_t i = 0;
             i < sizeof(message) - 1;
             ++i) {
            std::cout
                << static_cast<char>(
                       restored.data()[i]
                   );
        }
    }

    std::cout << '\n';

    std::cout << "Checksum: "
              << original.checksum()
              << '\n';

    return deserialized ? 0 : 1;
}