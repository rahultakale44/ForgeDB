#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

namespace {

class BufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        database_path_ = "buffer_pool_test.db";

        std::filesystem::remove(database_path_);

        disk_manager_ =
            std::make_unique<
                forgedb::storage::DiskManager
            >(database_path_);
    }

    void TearDown() override {
        disk_manager_.reset();

        std::filesystem::remove(database_path_);
    }

    std::string database_path_;

    std::unique_ptr<
        forgedb::storage::DiskManager
    > disk_manager_;
};

TEST_F(BufferPoolTest, FetchesPageFromDisk) {
    forgedb::storage::Page page;
    page.set_id(1);
    page.data()[0] = std::byte{'A'};

    ASSERT_TRUE(
        disk_manager_->write_page(1, page)
    );

    forgedb::buffer::BufferPoolManager buffer_pool(
        2,
        *disk_manager_
    );

    auto* fetched =
        buffer_pool.fetch_page(1);

    ASSERT_NE(fetched, nullptr);

    EXPECT_EQ(fetched->id(), 1);

    EXPECT_EQ(
        static_cast<char>(fetched->data()[0]),
        'A'
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, false)
    );
}

TEST_F(BufferPoolTest, ReusesCachedPage) {
    forgedb::storage::Page page;
    page.set_id(1);
    page.data()[0] = std::byte{'X'};

    ASSERT_TRUE(
        disk_manager_->write_page(1, page)
    );

    forgedb::buffer::BufferPoolManager buffer_pool(
        2,
        *disk_manager_
    );

    auto* first =
        buffer_pool.fetch_page(1);

    ASSERT_NE(first, nullptr);

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, false)
    );

    auto* second =
        buffer_pool.fetch_page(1);

    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first, second);

    EXPECT_EQ(
        static_cast<char>(second->data()[0]),
        'X'
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, false)
    );
}

TEST_F(BufferPoolTest, DirtyPageIsFlushed) {
    forgedb::storage::Page page;
    page.set_id(1);
    page.data()[0] = std::byte{'A'};

    ASSERT_TRUE(
        disk_manager_->write_page(1, page)
    );

    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    auto* fetched =
        buffer_pool.fetch_page(1);

    ASSERT_NE(fetched, nullptr);

    fetched->data()[0] = std::byte{'B'};

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, true)
    );

    EXPECT_TRUE(
        buffer_pool.flush_page(1)
    );

    forgedb::storage::Page restored;

    ASSERT_TRUE(
        disk_manager_->read_page(1, restored)
    );

    EXPECT_EQ(
        static_cast<char>(restored.data()[0]),
        'B'
    );
}

TEST_F(BufferPoolTest, EvictsUnpinnedPage) {
    forgedb::storage::Page page_one;
    page_one.set_id(1);
    page_one.data()[0] = std::byte{'A'};

    forgedb::storage::Page page_two;
    page_two.set_id(2);
    page_two.data()[0] = std::byte{'B'};

    ASSERT_TRUE(
        disk_manager_->write_page(1, page_one)
    );

    ASSERT_TRUE(
        disk_manager_->write_page(2, page_two)
    );

    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    auto* first =
        buffer_pool.fetch_page(1);

    ASSERT_NE(first, nullptr);

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, false)
    );

    auto* second =
        buffer_pool.fetch_page(2);

    ASSERT_NE(second, nullptr);

    EXPECT_EQ(second->id(), 2);

    EXPECT_EQ(
        static_cast<char>(second->data()[0]),
        'B'
    );
}

TEST_F(BufferPoolTest, CannotEvictPinnedPage) {
    forgedb::storage::Page page_one;
    page_one.set_id(1);

    forgedb::storage::Page page_two;
    page_two.set_id(2);

    ASSERT_TRUE(
        disk_manager_->write_page(1, page_one)
    );

    ASSERT_TRUE(
        disk_manager_->write_page(2, page_two)
    );

    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    auto* first =
        buffer_pool.fetch_page(1);

    ASSERT_NE(first, nullptr);

    auto* second =
        buffer_pool.fetch_page(2);

    EXPECT_EQ(second, nullptr);

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        1
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(1, false)
    );
}

TEST_F(BufferPoolTest, CreatesNewPage) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        2,
        *disk_manager_
    );

    forgedb::storage::PageId page_id;

    auto* page =
        buffer_pool.new_page(page_id);

    ASSERT_NE(page, nullptr);

    EXPECT_EQ(page_id, 0);
    EXPECT_EQ(page->id(), 0);

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        1
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(page_id, false)
    );
}

TEST_F(BufferPoolTest, CreatesSequentialPages) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        2,
        *disk_manager_
    );

    forgedb::storage::PageId first_id;
    forgedb::storage::PageId second_id;

    auto* first_page =
        buffer_pool.new_page(first_id);

    auto* second_page =
        buffer_pool.new_page(second_id);

    ASSERT_NE(first_page, nullptr);
    ASSERT_NE(second_page, nullptr);

    EXPECT_EQ(first_id, 0);
    EXPECT_EQ(second_id, 1);

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        2
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(first_id, false)
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(second_id, false)
    );
}

TEST_F(BufferPoolTest, NewPageCanBeWrittenAndRead) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    forgedb::storage::PageId page_id;

    auto* page =
        buffer_pool.new_page(page_id);

    ASSERT_NE(page, nullptr);

    const std::string message =
        "ForgeDB new page test";

    for (std::size_t i = 0; i < message.size(); ++i) {
        page->data()[i] =
            static_cast<std::byte>(message[i]);
    }

    ASSERT_TRUE(
        buffer_pool.unpin_page(
            page_id,
            true
        )
    );

    ASSERT_TRUE(
        buffer_pool.flush_page(page_id)
    );

    auto* fetched_page =
        buffer_pool.fetch_page(page_id);

    ASSERT_NE(fetched_page, nullptr);

    for (std::size_t i = 0; i < message.size(); ++i) {
        EXPECT_EQ(
            static_cast<char>(
                fetched_page->data()[i]
            ),
            message[i]
        );
    }

    EXPECT_TRUE(
        buffer_pool.unpin_page(
            page_id,
            false
        )
    );
}

TEST_F(BufferPoolTest, NewPageEvictsUnpinnedPage) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    forgedb::storage::PageId first_id;

    auto* first_page =
        buffer_pool.new_page(first_id);

    ASSERT_NE(first_page, nullptr);
    EXPECT_EQ(first_id, 0);

    EXPECT_TRUE(
        buffer_pool.unpin_page(
            first_id,
            false
        )
    );

    forgedb::storage::PageId second_id;

    auto* second_page =
        buffer_pool.new_page(second_id);

    ASSERT_NE(second_page, nullptr);

    EXPECT_EQ(second_id, 1);
    EXPECT_EQ(second_page->id(), 1);

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        1
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(
            second_id,
            false
        )
    );
}

TEST_F(BufferPoolTest, NewPageFailsWhenAllFramesArePinned) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    forgedb::storage::PageId first_id;

    auto* first_page =
        buffer_pool.new_page(first_id);

    ASSERT_NE(first_page, nullptr);

    forgedb::storage::PageId second_id;

    auto* second_page =
        buffer_pool.new_page(second_id);

    EXPECT_EQ(second_page, nullptr);

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        1
    );

    EXPECT_EQ(
        disk_manager_->file_size(),
        forgedb::storage::PAGE_SIZE
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(
            first_id,
            false
        )
    );
}

TEST_F(BufferPoolTest, FailedNewPageDoesNotAllocateDiskSpace) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        1,
        *disk_manager_
    );

    forgedb::storage::PageId first_id;

    auto* first_page =
        buffer_pool.new_page(first_id);

    ASSERT_NE(first_page, nullptr);

    EXPECT_EQ(
        disk_manager_->file_size(),
        forgedb::storage::PAGE_SIZE
    );

    forgedb::storage::PageId failed_id;

    auto* failed_page =
        buffer_pool.new_page(failed_id);

    EXPECT_EQ(failed_page, nullptr);

    EXPECT_EQ(
        disk_manager_->file_size(),
        forgedb::storage::PAGE_SIZE
    );

    EXPECT_TRUE(
        buffer_pool.unpin_page(
            first_id,
            false
        )
    );
}

TEST_F(BufferPoolTest, NewPageFailsWithZeroSizedBufferPool) {
    forgedb::buffer::BufferPoolManager buffer_pool(
        0,
        *disk_manager_
    );

    forgedb::storage::PageId page_id;

    auto* page =
        buffer_pool.new_page(page_id);

    EXPECT_EQ(page, nullptr);

    EXPECT_EQ(
        disk_manager_->file_size(),
        0
    );

    EXPECT_EQ(
        buffer_pool.pinned_page_count(),
        0
    );
}

}  // namespace
