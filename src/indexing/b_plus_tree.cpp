#include "indexing/b_plus_tree.h"

#include <algorithm>

namespace forgedb::indexing {

BPlusTree::Node::Node(bool leaf)
    : is_leaf(leaf),
      keys(),
      values(),
      children(),
      next(nullptr) {}

BPlusTree::BPlusTree(std::size_t leaf_capacity)
    : leaf_capacity_(std::max<std::size_t>(2, leaf_capacity)),
      root_(std::make_unique<Node>(true)),
      size_(0) {}

bool BPlusTree::insert(Key key, Value value) {
    SplitResult split{};

    if (!insert_recursive(
            root_.get(),
            key,
            value,
            split)) {
        return false;
    }

    if (split.right) {
        auto new_root =
            std::make_unique<Node>(false);

        new_root->keys.push_back(
            split.separator
        );

        new_root->children.push_back(
            std::move(root_)
        );

        new_root->children.push_back(
            std::move(split.right)
        );

        root_ = std::move(new_root);
    }

    ++size_;

    return true;
}

bool BPlusTree::search(
    Key key,
    Value& value
) const {
    return search_recursive(
        root_.get(),
        key,
        value
    );
}

std::size_t BPlusTree::size() const {
    return size_;
}

bool BPlusTree::empty() const {
    return size_ == 0;
}

bool BPlusTree::insert_recursive(
    Node* node,
    Key key,
    Value value,
    SplitResult& split
) {
    if (node->is_leaf) {
        const auto position =
            std::lower_bound(
                node->keys.begin(),
                node->keys.end(),
                key
            );

        const std::size_t index =
            static_cast<std::size_t>(
                position - node->keys.begin()
            );

        if (position != node->keys.end() &&
            *position == key) {
            return false;
        }

        node->keys.insert(
            node->keys.begin() +
                static_cast<std::ptrdiff_t>(index),
            key
        );

        node->values.insert(
            node->values.begin() +
                static_cast<std::ptrdiff_t>(index),
            value
        );

        if (node->keys.size() <= leaf_capacity_) {
            return true;
        }

        split.separator = node->keys[
            node->keys.size() / 2
        ];

        split.right =
            split_leaf(
                node,
                split.separator
            );

        return true;
    }

    const std::size_t child_index =
        find_child_index(node, key);

    SplitResult child_split{};

    if (!insert_recursive(
            node->children[child_index].get(),
            key,
            value,
            child_split)) {
        return false;
    }

    if (!child_split.right) {
        return true;
    }

    node->keys.insert(
        node->keys.begin() +
            static_cast<std::ptrdiff_t>(child_index),
        child_split.separator
    );

    node->children.insert(
        node->children.begin() +
            static_cast<std::ptrdiff_t>(child_index + 1),
        std::move(child_split.right)
    );

    if (node->children.size() <= leaf_capacity_ + 1) {
        return true;
    }

    split.right =
        split_internal(
            node,
            split.separator
        );

    return true;
}

bool BPlusTree::search_recursive(
    const Node* node,
    Key key,
    Value& value
) const {
    if (node->is_leaf) {
        const auto position =
            std::lower_bound(
                node->keys.begin(),
                node->keys.end(),
                key
            );

        if (position == node->keys.end() ||
            *position != key) {
            return false;
        }

        const std::size_t index =
            static_cast<std::size_t>(
                position - node->keys.begin()
            );

        value = node->values[index];

        return true;
    }

    const std::size_t child_index =
        find_child_index(node, key);

    return search_recursive(
        node->children[child_index].get(),
        key,
        value
    );
}

std::unique_ptr<BPlusTree::Node>
BPlusTree::split_leaf(
    Node* node,
    Key& separator
) {
    auto right =
        std::make_unique<Node>(true);

    const std::size_t middle =
        node->keys.size() / 2;

    right->keys.assign(
        node->keys.begin() +
            static_cast<std::ptrdiff_t>(middle),
        node->keys.end()
    );

    right->values.assign(
        node->values.begin() +
            static_cast<std::ptrdiff_t>(middle),
        node->values.end()
    );

    node->keys.erase(
        node->keys.begin() +
            static_cast<std::ptrdiff_t>(middle),
        node->keys.end()
    );

    node->values.erase(
        node->values.begin() +
            static_cast<std::ptrdiff_t>(middle),
        node->values.end()
    );

    separator = right->keys.front();

    right->next = node->next;
    node->next = right.get();

    return right;
}

std::unique_ptr<BPlusTree::Node>
BPlusTree::split_internal(
    Node* node,
    Key& separator
) {
    auto right =
        std::make_unique<Node>(false);

    const std::size_t middle =
        node->keys.size() / 2;

    separator = node->keys[middle];

    right->keys.assign(
        node->keys.begin() +
            static_cast<std::ptrdiff_t>(middle + 1),
        node->keys.end()
    );

    right->children.reserve(
        node->children.size() -
        (middle + 1)
    );

    for (std::size_t i = middle + 1;
         i < node->children.size();
         ++i) {
        right->children.push_back(
            std::move(node->children[i])
        );
    }

    node->keys.erase(
        node->keys.begin() +
            static_cast<std::ptrdiff_t>(middle),
        node->keys.end()
    );

    node->children.erase(
        node->children.begin() +
            static_cast<std::ptrdiff_t>(middle + 1),
        node->children.end()
    );

    return right;
}

std::size_t BPlusTree::find_child_index(
    const Node* node,
    Key key
) const {
    return static_cast<std::size_t>(
        std::upper_bound(
            node->keys.begin(),
            node->keys.end(),
            key
        ) - node->keys.begin()
    );
}

}  // namespace forgedb::indexing
