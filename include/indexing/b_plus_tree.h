#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "storage/page.h"

namespace forgedb::indexing {

class BPlusTree {
public:
    using Key = std::int32_t;
    using Value = forgedb::storage::PageId;

    BPlusTree(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        std::size_t leaf_capacity = 4
    );

    BPlusTree(
        forgedb::buffer::BufferPoolManager& buffer_pool,
        forgedb::storage::PageId root_page_id,
        std::size_t leaf_capacity = 4
    );

    bool insert(Key key, Value value);

    bool search(Key key, Value& value);

    std::size_t size();

    bool empty();

    forgedb::storage::PageId root_page_id() const;

private:
    static constexpr std::size_t MAX_KEYS_PER_NODE = 340;

    struct NodeHeader {
        bool is_leaf;
        std::uint32_t num_keys;
        forgedb::storage::PageId next_page_id;
    };

    struct SplitResult {
        Key separator;
        forgedb::storage::PageId right_page_id;
    };

    forgedb::storage::PageId create_node(bool is_leaf);

    bool read_node(
        forgedb::storage::PageId page_id,
        NodeHeader& header,
        std::vector<Key>& keys,
        std::vector<Value>& values,
        std::vector<forgedb::storage::PageId>& children
    );

    bool write_node(
        forgedb::storage::PageId page_id,
        const NodeHeader& header,
        const std::vector<Key>& keys,
        const std::vector<Value>& values,
        const std::vector<forgedb::storage::PageId>& children
    );

    bool insert_recursive(
        forgedb::storage::PageId page_id,
        Key key,
        Value value,
        std::optional<SplitResult>& split
    );

    bool search_recursive(
        forgedb::storage::PageId page_id,
        Key key,
        Value& value
    );

    std::optional<forgedb::storage::PageId> split_leaf(
        forgedb::storage::PageId page_id,
        Key& separator
    );

    std::optional<forgedb::storage::PageId> split_internal(
        forgedb::storage::PageId page_id,
        Key& separator
    );

    std::size_t find_child_index(
        const std::vector<Key>& keys,
        Key key
    ) const;

    std::size_t compute_size_recursive(
        forgedb::storage::PageId page_id
    );

    forgedb::buffer::BufferPoolManager& buffer_pool_;
    std::size_t leaf_capacity_;
    forgedb::storage::PageId root_page_id_;
    std::optional<std::size_t> cached_size_;
};

}  // namespace forgedb::indexing
