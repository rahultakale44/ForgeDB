#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "storage/disk_manager.h"
#include "storage/page.h"

namespace {

class DiskManagerTest : public ::testing::Test {
protected:
    const std::string test_database = "test_forgedb.db";

    void SetUp() override {
        std::filesystem::remove(test_database);
    }

    void TearDown() override {
        std::filesystem::remove(test_database);
    }
};

TEST_F(DiskManagerTest, CreatesDatabaseFile) {
    {
        forgedb::storage::DiskManager disk_manager(test_database);
    }

    EXPECT_TRUE(std::filesystem::exists(test_database));
}

TEST_F(DiskManagerTest, WritesAndReadsPage) {
    forgedb::storage::DiskManager disk_manager(test_database);

    forgedb::storage::Page write_page;
    write_page.set_id(0);

    const std::string message = "ForgeDB storage test";

    for (std::size_t i = 0; i < message.size(); ++i) {
        write_page.data()[i] =
            static_cast<std::byte>(message[i]);
    }

    ASSERT_TRUE(
        disk_manager.write_page(0, write_page)
    );

    forgedb::storage::Page read_page;

    ASSERT_TRUE(
        disk_manager.read_page(0, read_page)
    );

    for (std::size_t i = 0; i < message.size(); ++i) {
        EXPECT_EQ(
            static_cast<char>(read_page.data()[i]),
            message[i]
        );
    }

    EXPECT_EQ(read_page.id(), 0);
}

TEST_F(DiskManagerTest, DataPersistsAfterReopen) {
    const std::string message =
        "Persistent ForgeDB data";

    {
        forgedb::storage::DiskManager disk_manager(
            test_database
        );

        forgedb::storage::Page page;
        page.set_id(0);

        for (std::size_t i = 0; i < message.size(); ++i) {
            page.data()[i] =
                static_cast<std::byte>(message[i]);
        }

        ASSERT_TRUE(
            disk_manager.write_page(0, page)
        );
    }

    {
        forgedb::storage::DiskManager disk_manager(
            test_database
        );

        forgedb::storage::Page page;

        ASSERT_TRUE(
            disk_manager.read_page(0, page)
        );

        for (std::size_t i = 0; i < message.size(); ++i) {
            EXPECT_EQ(
                static_cast<char>(page.data()[i]),
                message[i]
            );
        }
    }
}

TEST_F(DiskManagerTest, MultiplePagesRemainIndependent) {
    forgedb::storage::DiskManager disk_manager(
        test_database
    );

    forgedb::storage::Page page_zero;
    forgedb::storage::Page page_one;

    page_zero.set_id(0);
    page_one.set_id(1);

    page_zero.data()[0] = std::byte{'A'};
    page_one.data()[0] = std::byte{'B'};

    ASSERT_TRUE(
        disk_manager.write_page(0, page_zero)
    );

    ASSERT_TRUE(
        disk_manager.write_page(1, page_one)
    );

    forgedb::storage::Page read_zero;
    forgedb::storage::Page read_one;

    ASSERT_TRUE(
        disk_manager.read_page(0, read_zero)
    );

    ASSERT_TRUE(
        disk_manager.read_page(1, read_one)
    );

    EXPECT_EQ(
        static_cast<char>(read_zero.data()[0]),
        'A'
    );

    EXPECT_EQ(
        static_cast<char>(read_one.data()[0]),
        'B'
    );
}

TEST_F(DiskManagerTest, FileSizeMatchesPageCount) {
    forgedb::storage::DiskManager disk_manager(
        test_database
    );

    forgedb::storage::Page page_zero;
    forgedb::storage::Page page_one;

    ASSERT_TRUE(
        disk_manager.write_page(0, page_zero)
    );

    ASSERT_TRUE(
        disk_manager.write_page(1, page_one)
    );

    EXPECT_EQ(
        disk_manager.file_size(),
        2 * forgedb::storage::PAGE_SIZE
    );
}

TEST_F(DiskManagerTest, AllocatesFirstPage) {
    forgedb::storage::DiskManager disk_manager(
        test_database
    );

    const auto page_id =
        disk_manager.allocate_page();

    EXPECT_EQ(page_id, 0);

    EXPECT_EQ(
        disk_manager.file_size(),
        forgedb::storage::PAGE_SIZE
    );
}

TEST_F(DiskManagerTest, AllocatesSequentialPages) {
    forgedb::storage::DiskManager disk_manager(
        test_database
    );

    const auto first =
        disk_manager.allocate_page();

    const auto second =
        disk_manager.allocate_page();

    const auto third =
        disk_manager.allocate_page();

    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(third, 2);

    EXPECT_EQ(
        disk_manager.file_size(),
        3 * forgedb::storage::PAGE_SIZE
    );
}

TEST_F(DiskManagerTest, AllocatedPageCanBeRead) {
    forgedb::storage::DiskManager disk_manager(
        test_database
    );

    const auto page_id =
        disk_manager.allocate_page();

    ASSERT_EQ(page_id, 0);

    forgedb::storage::Page page;

    ASSERT_TRUE(
        disk_manager.read_page(page_id, page)
    );

    EXPECT_EQ(page.id(), page_id);
}

TEST(PageSerializationTest, RoundTripPreservesData) {
    forgedb::storage::Page original;
    original.set_id(42);

    const std::string message =
        "ForgeDB serialization";

    for (std::size_t i = 0; i < message.size(); ++i) {
        original.data()[i] =
            static_cast<std::byte>(message[i]);
    }

    std::array<
        std::byte,
        forgedb::storage::PAGE_SIZE
    > buffer{};

    ASSERT_TRUE(
        original.serialize(buffer)
    );

    forgedb::storage::Page restored;

    ASSERT_TRUE(
        restored.deserialize(buffer)
    );

    EXPECT_EQ(restored.id(), 42);

    for (std::size_t i = 0; i < message.size(); ++i) {
        EXPECT_EQ(
            static_cast<char>(restored.data()[i]),
            message[i]
        );
    }
}

TEST(PageSerializationTest, RejectsInvalidMagic) {
    forgedb::storage::Page page;

    std::array<
        std::byte,
        forgedb::storage::PAGE_SIZE
    > buffer{};

    ASSERT_TRUE(
        page.serialize(buffer)
    );

    buffer[0] = std::byte{0};

    forgedb::storage::Page restored;

    EXPECT_FALSE(
        restored.deserialize(buffer)
    );
}

TEST(PageSerializationTest, RejectsCorruptedPayload) {
    forgedb::storage::Page page;
    page.set_id(7);

    page.data()[0] = std::byte{'X'};

    std::array<
        std::byte,
        forgedb::storage::PAGE_SIZE
    > buffer{};

    ASSERT_TRUE(
        page.serialize(buffer)
    );

    constexpr std::size_t HEADER_SIZE =
        sizeof(forgedb::storage::PageHeader);

    buffer[HEADER_SIZE] ^= std::byte{0xFF};

    forgedb::storage::Page restored;

    EXPECT_FALSE(
        restored.deserialize(buffer)
    );
}

}  // namespace