#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace forgedb::storage {

enum class ValueType : std::uint8_t {
    INTEGER,
    BOOLEAN,
    VARCHAR
};

class Value {
public:
    Value();
    explicit Value(std::int32_t int_value);
    explicit Value(bool bool_value);
    explicit Value(const std::string& string_value);

    ValueType type() const;

    std::optional<std::int32_t> as_int() const;
    std::optional<bool> as_bool() const;
    std::optional<std::string> as_string() const;

    bool is_null() const;

    std::size_t serialized_size() const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const;

private:
    ValueType type_;
    bool is_null_;
    std::variant<std::int32_t, bool, std::string> data_;
};

class Schema {
public:
    Schema() = default;

    void add_column(ValueType type, std::size_t max_length = 0);

    std::size_t column_count() const;
    ValueType column_type(std::size_t index) const;
    std::size_t column_max_length(std::size_t index) const;

    std::size_t fixed_tuple_size() const;

private:
    struct ColumnInfo {
        ValueType type;
        std::size_t max_length;
    };

    std::vector<ColumnInfo> columns_;
};

class Tuple {
public:
    explicit Tuple(const Schema& schema);

    void set_value(std::size_t index, const Value& value);
    const Value& get_value(std::size_t index) const;

    std::size_t value_count() const;

    std::size_t serialized_size() const;

    bool serialize(std::byte* buffer, std::size_t buffer_size) const;
    bool deserialize(
        const std::byte* buffer,
        std::size_t buffer_size,
        const Schema& schema
    );

private:
    std::vector<Value> values_;
};

using RID = std::uint32_t;

}  // namespace forgedb::storage
