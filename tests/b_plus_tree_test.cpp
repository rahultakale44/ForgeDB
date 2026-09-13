#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "indexing/b_plus_tree.h"
#include "storage/disk_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::indexing::BPlusTree;
using forgedb::storage::DiskManager;
using forgedb::storage::PageId;

class BPlusTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_bptree.db";

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }
    }

    std::string test_db_path_;
};

TEST_F(BPlusTreeTest, StartsEmpty) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0);
}

TEST_F(BPlusTreeTest, InsertsAndSearchesKey) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(10, 42));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(tree.size(), 1);
}

TEST_F(BPlusTreeTest, SearchesMultipleKeys) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(10, 100));
    EXPECT_TRUE(tree.insert(20, 200));
    EXPECT_TRUE(tree.insert(30, 300));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 100);

    EXPECT_TRUE(tree.search(20, value));
    EXPECT_EQ(value, 200);

    EXPECT_TRUE(tree.search(30, value));
    EXPECT_EQ(value, 300);

    EXPECT_EQ(tree.size(), 3);
}

TEST_F(BPlusTreeTest, ReturnsFalseForMissingKey) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(10, 100));

    BPlusTree::Value value = 0;

    EXPECT_FALSE(tree.search(20, value));
}

TEST_F(BPlusTreeTest, RejectsDuplicateKeys) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(10, 100));
    EXPECT_FALSE(tree.insert(10, 200));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 100);

    EXPECT_EQ(tree.size(), 1);
}

TEST_F(BPlusTreeTest, HandlesKeysInsertedOutOfOrder) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(40, 400));
    EXPECT_TRUE(tree.insert(10, 100));
    EXPECT_TRUE(tree.insert(30, 300));
    EXPECT_TRUE(tree.insert(20, 200));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 100);

    EXPECT_TRUE(tree.search(20, value));
    EXPECT_EQ(value, 200);

    EXPECT_TRUE(tree.search(30, value));
    EXPECT_EQ(value, 300);

    EXPECT_TRUE(tree.search(40, value));
    EXPECT_EQ(value, 400);
}

TEST_F(BPlusTreeTest, SplitsLeafNode) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool, std::size_t{4});

    EXPECT_TRUE(tree.insert(10, 100));
    EXPECT_TRUE(tree.insert(20, 200));
    EXPECT_TRUE(tree.insert(30, 300));
    EXPECT_TRUE(tree.insert(40, 400));
    EXPECT_TRUE(tree.insert(50, 500));

    EXPECT_EQ(tree.size(), 5);

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 100);

    EXPECT_TRUE(tree.search(20, value));
    EXPECT_EQ(value, 200);

    EXPECT_TRUE(tree.search(30, value));
    EXPECT_EQ(value, 300);

    EXPECT_TRUE(tree.search(40, value));
    EXPECT_EQ(value, 400);

    EXPECT_TRUE(tree.search(50, value));
    EXPECT_EQ(value, 500);
}

TEST_F(BPlusTreeTest, SplitsRootIntoInternalNode) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(20, disk_manager);
    BPlusTree tree(buffer_pool, std::size_t{3});

    for (std::int32_t key = 1; key <= 10; ++key) {
        EXPECT_TRUE(
            tree.insert(
                key,
                static_cast<BPlusTree::Value>(key * 10)
            )
        );
    }

    EXPECT_EQ(tree.size(), 10);

    BPlusTree::Value value = 0;

    for (std::int32_t key = 1; key <= 10; ++key) {
        EXPECT_TRUE(tree.search(key, value));
        EXPECT_EQ(
            value,
            static_cast<BPlusTree::Value>(key * 10)
        );
    }
}

TEST_F(BPlusTreeTest, HandlesMultipleSplits) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(50, disk_manager);
    BPlusTree tree(buffer_pool, std::size_t{3});

    for (std::int32_t key = 1; key <= 100; ++key) {
        EXPECT_TRUE(
            tree.insert(
                key,
                static_cast<BPlusTree::Value>(key + 1000)
            )
        );
    }

    EXPECT_EQ(tree.size(), 100);

    BPlusTree::Value value = 0;

    for (std::int32_t key = 1; key <= 100; ++key) {
        EXPECT_TRUE(tree.search(key, value));
        EXPECT_EQ(
            value,
            static_cast<BPlusTree::Value>(key + 1000)
        );
    }
}

TEST_F(BPlusTreeTest, HandlesNegativeKeys) {
    DiskManager disk_manager(test_db_path_);
    BufferPoolManager buffer_pool(10, disk_manager);
    BPlusTree tree(buffer_pool);

    EXPECT_TRUE(tree.insert(-30, 300));
    EXPECT_TRUE(tree.insert(-10, 100));
    EXPECT_TRUE(tree.insert(-20, 200));
    EXPECT_TRUE(tree.insert(0, 400));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(-30, value));
    EXPECT_EQ(value, 300);

    EXPECT_TRUE(tree.search(-20, value));
    EXPECT_EQ(value, 200);

    EXPECT_TRUE(tree.search(-10, value));
    EXPECT_EQ(value, 100);

    EXPECT_TRUE(tree.search(0, value));
    EXPECT_EQ(value, 400);
}

TEST_F(BPlusTreeTest, PersistsDataAcrossReopen) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(10, disk_manager);
        BPlusTree tree(buffer_pool);

        EXPECT_TRUE(tree.insert(10, 100));
        EXPECT_TRUE(tree.insert(20, 200));
        EXPECT_TRUE(tree.insert(30, 300));

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(10, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id);

        BPlusTree::Value value = 0;

        EXPECT_TRUE(tree.search(10, value));
        EXPECT_EQ(value, 100);

        EXPECT_TRUE(tree.search(20, value));
        EXPECT_EQ(value, 200);

        EXPECT_TRUE(tree.search(30, value));
        EXPECT_EQ(value, 300);

        EXPECT_FALSE(tree.search(40, value));
    }
}

TEST_F(BPlusTreeTest, PersistsLargeDataset) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, std::size_t{4});

        for (std::int32_t key = 1; key <= 50; ++key) {
            EXPECT_TRUE(
                tree.insert(
                    key,
                    static_cast<BPlusTree::Value>(key * 100)
                )
            );
        }

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id, std::size_t{4});

        BPlusTree::Value value = 0;

        for (std::int32_t key = 1; key <= 50; ++key) {
            EXPECT_TRUE(tree.search(key, value));
            EXPECT_EQ(
                value,
                static_cast<BPlusTree::Value>(key * 100)
            );
        }

        EXPECT_FALSE(tree.search(51, value));
        EXPECT_FALSE(tree.search(0, value));
    }
}

TEST_F(BPlusTreeTest, ContinuesInsertionAfterReopen) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, std::size_t{4});

        for (std::int32_t key = 1; key <= 25; ++key) {
            EXPECT_TRUE(
                tree.insert(
                    key,
                    static_cast<BPlusTree::Value>(key * 10)
                )
            );
        }

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id, std::size_t{4});

        for (std::int32_t key = 26; key <= 50; ++key) {
            EXPECT_TRUE(
                tree.insert(
                    key,
                    static_cast<BPlusTree::Value>(key * 10)
                )
            );
        }

        buffer_pool.flush_all_pages();

        BPlusTree::Value value = 0;

        for (std::int32_t key = 1; key <= 50; ++key) {
            EXPECT_TRUE(tree.search(key, value));
            EXPECT_EQ(
                value,
                static_cast<BPlusTree::Value>(key * 10)
            );
        }
    }
}

TEST_F(BPlusTreeTest, HandlesMultipleLevelsWithPersistence) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(30, disk_manager);
        BPlusTree tree(buffer_pool, std::size_t{3});

        for (std::int32_t key = 1; key <= 100; ++key) {
            EXPECT_TRUE(
                tree.insert(
                    key,
                    static_cast<BPlusTree::Value>(key + 5000)
                )
            );
        }

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(30, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id, std::size_t{3});

        BPlusTree::Value value = 0;

        for (std::int32_t key = 1; key <= 100; ++key) {
            EXPECT_TRUE(tree.search(key, value));
            EXPECT_EQ(
                value,
                static_cast<BPlusTree::Value>(key + 5000)
            );
        }

        EXPECT_EQ(tree.size(), 100);
    }
}

TEST_F(BPlusTreeTest, HandlesDuplicateRejectionAfterReopen) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(10, disk_manager);
        BPlusTree tree(buffer_pool);

        EXPECT_TRUE(tree.insert(10, 100));
        EXPECT_TRUE(tree.insert(20, 200));

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(10, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id);

        EXPECT_FALSE(tree.insert(10, 999));
        EXPECT_FALSE(tree.insert(20, 888));

        BPlusTree::Value value = 0;

        EXPECT_TRUE(tree.search(10, value));
        EXPECT_EQ(value, 100);

        EXPECT_TRUE(tree.search(20, value));
        EXPECT_EQ(value, 200);
    }
}

TEST_F(BPlusTreeTest, HandlesOutOfOrderInsertionAfterReopen) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, std::size_t{4});

        EXPECT_TRUE(tree.insert(50, 500));
        EXPECT_TRUE(tree.insert(30, 300));
        EXPECT_TRUE(tree.insert(70, 700));
        EXPECT_TRUE(tree.insert(10, 100));

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id, std::size_t{4});

        EXPECT_TRUE(tree.insert(40, 400));
        EXPECT_TRUE(tree.insert(20, 200));
        EXPECT_TRUE(tree.insert(60, 600));

        buffer_pool.flush_all_pages();

        BPlusTree::Value value = 0;

        EXPECT_TRUE(tree.search(10, value));
        EXPECT_EQ(value, 100);

        EXPECT_TRUE(tree.search(20, value));
        EXPECT_EQ(value, 200);

        EXPECT_TRUE(tree.search(30, value));
        EXPECT_EQ(value, 300);

        EXPECT_TRUE(tree.search(40, value));
        EXPECT_EQ(value, 400);

        EXPECT_TRUE(tree.search(50, value));
        EXPECT_EQ(value, 500);

        EXPECT_TRUE(tree.search(60, value));
        EXPECT_EQ(value, 600);

        EXPECT_TRUE(tree.search(70, value));
        EXPECT_EQ(value, 700);
    }
}

TEST_F(BPlusTreeTest, PersistsLeafSiblingRelationships) {
    PageId root_page_id = 0;

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, std::size_t{3});

        for (std::int32_t key = 1; key <= 20; ++key) {
            EXPECT_TRUE(
                tree.insert(
                    key,
                    static_cast<BPlusTree::Value>(key * 7)
                )
            );
        }

        root_page_id = tree.root_page_id();

        buffer_pool.flush_all_pages();
    }

    {
        DiskManager disk_manager(test_db_path_);
        BufferPoolManager buffer_pool(20, disk_manager);
        BPlusTree tree(buffer_pool, root_page_id, std::size_t{3});

        BPlusTree::Value value = 0;

        for (std::int32_t key = 1; key <= 20; ++key) {
            EXPECT_TRUE(tree.search(key, value));
            EXPECT_EQ(
                value,
                static_cast<BPlusTree::Value>(key * 7)
            );
        }

        EXPECT_EQ(tree.size(), 20);
    }
}

}  // namespace
