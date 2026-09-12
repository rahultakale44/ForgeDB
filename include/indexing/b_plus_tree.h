#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "storage/page.h"

namespace forgedb::indexing {

class BPlusTree {
public:
    using Key = std::int32_t;
    using Value = forgedb::storage::PageId;

    explicit BPlusTree(std::size_t leaf_capacity = 4);

    bool insert(Key key, Value value);

    bool search(Key key, Value& value) const;

    std::size_t size() const;

    bool empty() const;

private:
    struct Node {
        bool is_leaf;
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<std::unique_ptr<Node>> children;
        Node* next;

        explicit Node(bool leaf);
    };

    struct SplitResult {
        Key separator;
        std::unique_ptr<Node> right;
    };

    bool insert_recursive(
        Node* node,
        Key key,
        Value value,
        SplitResult& split
    );

    bool search_recursive(
        const Node* node,
        Key key,
        Value& value
    ) const;

    std::unique_ptr<Node> split_leaf(
        Node* node,
        Key& separator
    );

    std::unique_ptr<Node> split_internal(
        Node* node,
        Key& separator
    );

    std::size_t find_child_index(
        const Node* node,
        Key key
    ) const;

    std::size_t leaf_capacity_;
    std::unique_ptr<Node> root_;
    std::size_t size_;
};

}  // namespace forgedb::indexing
