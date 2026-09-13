#include "indexing/b_plus_tree.h"

#include <algorithm>
#include <cstring>

namespace forgedb::indexing {

BPlusTree::BPlusTree(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    std::size_t leaf_capacity
)
    : buffer_pool_(buffer_pool),
      leaf_capacity_(std::max<std::size_t>(2, leaf_capacity)),
      root_page_id_(0),
      cached_size_(0) {
    root_page_id_ = create_node(true);
}

BPlusTree::BPlusTree(
    forgedb::buffer::BufferPoolManager& buffer_pool,
    forgedb::storage::PageId root_page_id,
    std::size_t leaf_capacity
)
    : buffer_pool_(buffer_pool),
      leaf_capacity_(std::max<std::size_t>(2, leaf_capacity)),
      root_page_id_(root_page_id),
      cached_size_(std::nullopt) {}

bool BPlusTree::insert(Key key, Value value) {
    std::optional<SplitResult> split = std::nullopt;

    if (!insert_recursive(root_page_id_, key, value, split)) {
        return false;
    }

    if (split.has_value()) {
        const forgedb::storage::PageId new_root_page_id =
            create_node(false);

        if (new_root_page_id == 0) {
            return false;
        }

        NodeHeader header{};
        header.is_leaf = false;
        header.num_keys = 1;
        header.next_page_id = 0;

        std::vector<Key> keys = {split->separator};
        std::vector<Value> values;
        std::vector<forgedb::storage::PageId> children = {
            root_page_id_,
            split->right_page_id
        };

        if (!write_node(
                new_root_page_id,
                header,
                keys,
                values,
                children)) {
            return false;
        }

        root_page_id_ = new_root_page_id;
    }

    if (cached_size_.has_value()) {
        ++(*cached_size_);
    }

    return true;
}

bool BPlusTree::search(Key key, Value& value) {
    return search_recursive(root_page_id_, key, value);
}

std::size_t BPlusTree::size() {
    if (!cached_size_.has_value()) {
        cached_size_ = compute_size_recursive(root_page_id_);
    }

    return *cached_size_;
}

bool BPlusTree::empty() {
    return size() == 0;
}

forgedb::storage::PageId BPlusTree::root_page_id() const {
    return root_page_id_;
}

forgedb::storage::PageId BPlusTree::create_node(bool is_leaf) {
    forgedb::storage::PageId page_id = 0;

    forgedb::storage::Page* page =
        buffer_pool_.new_page(page_id);

    if (page == nullptr) {
        return 0;
    }

    NodeHeader header{};
    header.is_leaf = is_leaf;
    header.num_keys = 0;
    header.next_page_id = 0;

    std::byte* data = page->data();

    std::memcpy(data, &header, sizeof(NodeHeader));

    buffer_pool_.unpin_page(page_id, true);

    return page_id;
}

bool BPlusTree::read_node(
    forgedb::storage::PageId page_id,
    NodeHeader& header,
    std::vector<Key>& keys,
    std::vector<Value>& values,
    std::vector<forgedb::storage::PageId>& children
) {
    forgedb::storage::Page* page =
        buffer_pool_.fetch_page(page_id);

    if (page == nullptr) {
        return false;
    }

    std::byte* data = page->data();

    std::memcpy(&header, data, sizeof(NodeHeader));

    data += sizeof(NodeHeader);

    keys.clear();
    keys.resize(header.num_keys);

    std::memcpy(
        keys.data(),
        data,
        header.num_keys * sizeof(Key)
    );

    data += header.num_keys * sizeof(Key);

    values.clear();

    if (header.is_leaf) {
        values.resize(header.num_keys);

        std::memcpy(
            values.data(),
            data,
            header.num_keys * sizeof(Value)
        );
    } else {
        children.clear();
        children.resize(header.num_keys + 1);

        std::memcpy(
            children.data(),
            data,
            (header.num_keys + 1) *
                sizeof(forgedb::storage::PageId)
        );
    }

    buffer_pool_.unpin_page(page_id, false);

    return true;
}

bool BPlusTree::write_node(
    forgedb::storage::PageId page_id,
    const NodeHeader& header,
    const std::vector<Key>& keys,
    const std::vector<Value>& values,
    const std::vector<forgedb::storage::PageId>& children
) {
    forgedb::storage::Page* page =
        buffer_pool_.fetch_page(page_id);

    if (page == nullptr) {
        return false;
    }

    std::byte* data = page->data();

    std::memcpy(data, &header, sizeof(NodeHeader));

    data += sizeof(NodeHeader);

    std::memcpy(
        data,
        keys.data(),
        keys.size() * sizeof(Key)
    );

    data += keys.size() * sizeof(Key);

    if (header.is_leaf) {
        std::memcpy(
            data,
            values.data(),
            values.size() * sizeof(Value)
        );
    } else {
        std::memcpy(
            data,
            children.data(),
            children.size() *
                sizeof(forgedb::storage::PageId)
        );
    }

    buffer_pool_.unpin_page(page_id, true);

    return true;
}

bool BPlusTree::insert_recursive(
    forgedb::storage::PageId page_id,
    Key key,
    Value value,
    std::optional<SplitResult>& split
) {
    NodeHeader header{};
    std::vector<Key> keys;
    std::vector<Value> values;
    std::vector<forgedb::storage::PageId> children;

    if (!read_node(page_id, header, keys, values, children)) {
        return false;
    }

    if (header.is_leaf) {
        const auto position =
            std::lower_bound(keys.begin(), keys.end(), key);

        const std::size_t index =
            static_cast<std::size_t>(position - keys.begin());

        if (position != keys.end() && *position == key) {
            return false;
        }

        keys.insert(
            keys.begin() + static_cast<std::ptrdiff_t>(index),
            key
        );

        values.insert(
            values.begin() + static_cast<std::ptrdiff_t>(index),
            value
        );

        header.num_keys = static_cast<std::uint32_t>(keys.size());

        if (!write_node(page_id, header, keys, values, children)) {
            return false;
        }

        if (keys.size() <= leaf_capacity_) {
            split = std::nullopt;
            return true;
        }

        Key separator = 0;
        const auto right_page_id = split_leaf(page_id, separator);

        if (!right_page_id.has_value()) {
            return false;
        }

        split = SplitResult{separator, *right_page_id};

        return true;
    }

    const std::size_t child_index =
        find_child_index(keys, key);

    std::optional<SplitResult> child_split = std::nullopt;

    if (!insert_recursive(
            children[child_index],
            key,
            value,
            child_split)) {
        return false;
    }

    if (!child_split.has_value()) {
        split = std::nullopt;
        return true;
    }

    if (!read_node(page_id, header, keys, values, children)) {
        return false;
    }

    keys.insert(
        keys.begin() + static_cast<std::ptrdiff_t>(child_index),
        child_split->separator
    );

    children.insert(
        children.begin() +
            static_cast<std::ptrdiff_t>(child_index + 1),
        child_split->right_page_id
    );

    header.num_keys = static_cast<std::uint32_t>(keys.size());

    if (!write_node(page_id, header, keys, values, children)) {
        return false;
    }

    if (children.size() <= leaf_capacity_ + 1) {
        split = std::nullopt;
        return true;
    }

    Key separator = 0;
    const auto right_page_id = split_internal(page_id, separator);

    if (!right_page_id.has_value()) {
        return false;
    }

    split = SplitResult{separator, *right_page_id};

    return true;
}

bool BPlusTree::search_recursive(
    forgedb::storage::PageId page_id,
    Key key,
    Value& value
) {
    NodeHeader header{};
    std::vector<Key> keys;
    std::vector<Value> values;
    std::vector<forgedb::storage::PageId> children;

    if (!read_node(page_id, header, keys, values, children)) {
        return false;
    }

    if (header.is_leaf) {
        const auto position =
            std::lower_bound(keys.begin(), keys.end(), key);

        if (position == keys.end() || *position != key) {
            return false;
        }

        const std::size_t index =
            static_cast<std::size_t>(position - keys.begin());

        value = values[index];

        return true;
    }

    const std::size_t child_index =
        find_child_index(keys, key);

    const forgedb::storage::PageId child_page_id =
        children[child_index];

    return search_recursive(child_page_id, key, value);
}

std::optional<forgedb::storage::PageId> BPlusTree::split_leaf(
    forgedb::storage::PageId page_id,
    Key& separator
) {
    NodeHeader header{};
    std::vector<Key> keys;
    std::vector<Value> values;
    std::vector<forgedb::storage::PageId> children;

    if (!read_node(page_id, header, keys, values, children)) {
        return std::nullopt;
    }

    const std::size_t middle = keys.size() / 2;

    const forgedb::storage::PageId right_page_id =
        create_node(true);

    if (right_page_id == 0) {
        return std::nullopt;
    }

    NodeHeader right_header{};
    right_header.is_leaf = true;
    right_header.num_keys =
        static_cast<std::uint32_t>(keys.size() - middle);
    right_header.next_page_id = header.next_page_id;

    std::vector<Key> right_keys(
        keys.begin() + static_cast<std::ptrdiff_t>(middle),
        keys.end()
    );

    std::vector<Value> right_values(
        values.begin() + static_cast<std::ptrdiff_t>(middle),
        values.end()
    );

    std::vector<forgedb::storage::PageId> right_children;

    if (!write_node(
            right_page_id,
            right_header,
            right_keys,
            right_values,
            right_children)) {
        return std::nullopt;
    }

    keys.erase(
        keys.begin() + static_cast<std::ptrdiff_t>(middle),
        keys.end()
    );

    values.erase(
        values.begin() + static_cast<std::ptrdiff_t>(middle),
        values.end()
    );

    header.num_keys = static_cast<std::uint32_t>(keys.size());
    header.next_page_id = right_page_id;

    if (!write_node(page_id, header, keys, values, children)) {
        return std::nullopt;
    }

    separator = right_keys.front();

    return right_page_id;
}

std::optional<forgedb::storage::PageId> BPlusTree::split_internal(
    forgedb::storage::PageId page_id,
    Key& separator
) {
    NodeHeader header{};
    std::vector<Key> keys;
    std::vector<Value> values;
    std::vector<forgedb::storage::PageId> children;

    if (!read_node(page_id, header, keys, values, children)) {
        return std::nullopt;
    }

    const std::size_t middle = keys.size() / 2;

    const forgedb::storage::PageId right_page_id =
        create_node(false);

    if (right_page_id == 0) {
        return std::nullopt;
    }

    separator = keys[middle];

    NodeHeader right_header{};
    right_header.is_leaf = false;
    right_header.num_keys =
        static_cast<std::uint32_t>(keys.size() - middle - 1);
    right_header.next_page_id = 0;

    std::vector<Key> right_keys(
        keys.begin() + static_cast<std::ptrdiff_t>(middle + 1),
        keys.end()
    );

    std::vector<Value> right_values;

    std::vector<forgedb::storage::PageId> right_children(
        children.begin() + static_cast<std::ptrdiff_t>(middle + 1),
        children.end()
    );

    if (!write_node(
            right_page_id,
            right_header,
            right_keys,
            right_values,
            right_children)) {
        return std::nullopt;
    }

    keys.erase(
        keys.begin() + static_cast<std::ptrdiff_t>(middle),
        keys.end()
    );

    children.erase(
        children.begin() + static_cast<std::ptrdiff_t>(middle + 1),
        children.end()
    );

    header.num_keys = static_cast<std::uint32_t>(keys.size());

    if (!write_node(page_id, header, keys, values, children)) {
        return std::nullopt;
    }

    return right_page_id;
}

std::size_t BPlusTree::find_child_index(
    const std::vector<Key>& keys,
    Key key
) const {
    return static_cast<std::size_t>(
        std::upper_bound(keys.begin(), keys.end(), key) -
        keys.begin()
    );
}

std::size_t BPlusTree::compute_size_recursive(
    forgedb::storage::PageId page_id
) {
    NodeHeader header{};
    std::vector<Key> keys;
    std::vector<Value> values;
    std::vector<forgedb::storage::PageId> children;

    if (!read_node(page_id, header, keys, values, children)) {
        return 0;
    }

    if (header.is_leaf) {
        return keys.size();
    }

    std::size_t total = 0;

    for (const auto child_page_id : children) {
        total += compute_size_recursive(child_page_id);
    }

    return total;
}

}  // namespace forgedb::indexing
