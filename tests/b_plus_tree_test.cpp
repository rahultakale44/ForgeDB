#include <cstdint>

#include <gtest/gtest.h>

#include "indexing/b_plus_tree.h"

namespace {

using forgedb::indexing::BPlusTree;

TEST(BPlusTreeTest, StartsEmpty) {
    BPlusTree tree;

    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0);
}

TEST(BPlusTreeTest, InsertsAndSearchesKey) {
    BPlusTree tree;

    EXPECT_TRUE(tree.insert(10, 42));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(tree.size(), 1);
}

TEST(BPlusTreeTest, SearchesMultipleKeys) {
    BPlusTree tree;

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

TEST(BPlusTreeTest, ReturnsFalseForMissingKey) {
    BPlusTree tree;

    EXPECT_TRUE(tree.insert(10, 100));

    BPlusTree::Value value = 0;

    EXPECT_FALSE(tree.search(20, value));
}

TEST(BPlusTreeTest, RejectsDuplicateKeys) {
    BPlusTree tree;

    EXPECT_TRUE(tree.insert(10, 100));
    EXPECT_FALSE(tree.insert(10, 200));

    BPlusTree::Value value = 0;

    EXPECT_TRUE(tree.search(10, value));
    EXPECT_EQ(value, 100);

    EXPECT_EQ(tree.size(), 1);
}

TEST(BPlusTreeTest, HandlesKeysInsertedOutOfOrder) {
    BPlusTree tree;

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

TEST(BPlusTreeTest, SplitsLeafNode) {
    BPlusTree tree(4);

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

TEST(BPlusTreeTest, SplitsRootIntoInternalNode) {
    BPlusTree tree(3);

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

TEST(BPlusTreeTest, HandlesMultipleSplits) {
    BPlusTree tree(3);

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

TEST(BPlusTreeTest, HandlesNegativeKeys) {
    BPlusTree tree;

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

}  // namespace
