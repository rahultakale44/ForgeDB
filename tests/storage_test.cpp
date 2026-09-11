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

    ASSERT_TRUE(disk_manager.write_page(0, write_page));

    forgedb::storage::Page read_page;

    ASSERT_TRUE(disk_manager.read_page(0, read_page));

    for (std::size_t i = 0; i < message.size(); ++i) {
        EXPECT_EQ(
            static_cast<char>(read_page.data()[i]),
            message[i]
        );
    }

    EXPECT_EQ(read_page.id(), 0);
}

TEST_F(DiskManagerTest, DataPersistsAfterReopen) {
    const std::string message = "Persistent ForgeDB data";

    {
        forgedb::storage::DiskManager disk_manager(test_database);

        forgedb::storage::Page page;
        page.set_id(0);

        for (std::size_t i = 0; i < message.size(); ++i) {
            page.data()[i] =
                static_cast<std::byte>(message[i]);
        }

        ASSERT_TRUE(disk_manager.write_page(0, page));
    }

    {
        forgedb::storage::DiskManager disk_manager(test_database);

        forgedb::storage::Page page;

        ASSERT_TRUE(disk_manager.read_page(0, page));

        for (std::size_t i = 0; i < message.size(); ++i) {
            EXPECT_EQ(
                static_cast<char>(page.data()[i]),
                message[i]
            );
        }
    }
}

TEST_F(DiskManagerTest, MultiplePagesRemainIndependent) {
    forgedb::storage::DiskManager disk_manager(test_database);

    forgedb::storage::Page page_zero;
    forgedb::storage::Page page_one;

    page_zero.set_id(0);
    page_one.set_id(1);

    page_zero.data()[0] = std::byte{'A'};
    page_one.data()[0] = std::byte{'B'};

    ASSERT_TRUE(disk_manager.write_page(0, page_zero));
    ASSERT_TRUE(disk_manager.write_page(1, page_one));

    forgedb::storage::Page read_zero;
    forgedb::storage::Page read_one;

    ASSERT_TRUE(disk_manager.read_page(0, read_zero));
    ASSERT_TRUE(disk_manager.read_page(1, read_one));

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
    forgedb::storage::DiskManager disk_manager(test_database);

    forgedb::storage::Page page_zero;
    forgedb::storage::Page page_one;

    ASSERT_TRUE(disk_manager.write_page(0, page_zero));
    ASSERT_TRUE(disk_manager.write_page(1, page_one));

    EXPECT_EQ(
        disk_manager.file_size(),
        2 * forgedb::storage::PAGE_SIZE
    );
}

}  // namespace