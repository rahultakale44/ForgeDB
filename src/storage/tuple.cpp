#include "storage/tuple.h"

#include <cstring>

namespace forgedb::storage {

Value::Value()
    : type_(ValueType::INTEGER),
      is_null_(true),
      data_(std::int32_t{0}) {}

Value::Value(std::int32_t int_value)
    : type_(ValueType::INTEGER),
      is_null_(false),
      data_(int_value) {}

Value::Value(bool bool_value)
    : type_(ValueType::BOOLEAN),
      is_null_(false),
      data_(bool_value) {}

Value::Value(const std::string& string_value)
    : type_(ValueType::VARCHAR),
      is_null_(false),
      data_(string_value) {}

ValueType Value::type() const {
    return type_;
}

std::optional<std::int32_t> Value::as_int() const {
    if (is_null_ || type_ != ValueType::INTEGER) {
        return std::nullopt;
    }
    return std::get<std::int32_t>(data_);
}

std::optional<bool> Value::as_bool() const {
    if (is_null_ || type_ != ValueType::BOOLEAN) {
        return std::nullopt;
    }
    return std::get<bool>(data_);
}

std::optional<std::string> Value::as_string() const {
    if (is_null_ || type_ != ValueType::VARCHAR) {
        return std::nullopt;
    }
    return std::get<std::string>(data_);
}

bool Value::is_null() const {
    return is_null_;
}

std::size_t Value::serialized_size() const {
    std::size_t size = sizeof(std::uint8_t) + sizeof(std::uint8_t);

    if (is_null_) {
        return size;
    }

    switch (type_) {
        case ValueType::INTEGER:
            return size + sizeof(std::int32_t);
        case ValueType::BOOLEAN:
            return size + sizeof(std::uint8_t);
        case ValueType::VARCHAR: {
            const auto& str = std::get<std::string>(data_);
            return size + sizeof(std::uint32_t) + str.size();
        }
    }

    return size;
}

bool Value::operator==(const Value& other) const {
    if (type_ != other.type_ || is_null_ != other.is_null_) {
        return false;
    }

    if (is_null_) {
        return true;
    }

    return data_ == other.data_;
}

bool Value::operator!=(const Value& other) const {
    return !(*this == other);
}

void Schema::add_column(ValueType type, std::size_t max_length) {
    columns_.push_back({type, max_length});
}

std::size_t Schema::column_count() const {
    return columns_.size();
}

ValueType Schema::column_type(std::size_t index) const {
    return columns_.at(index).type;
}

std::size_t Schema::column_max_length(std::size_t index) const {
    return columns_.at(index).max_length;
}

std::size_t Schema::fixed_tuple_size() const {
    std::size_t size = 0;

    for (const auto& col : columns_) {
        size += sizeof(std::uint8_t) + sizeof(std::uint8_t);

        switch (col.type) {
            case ValueType::INTEGER:
                size += sizeof(std::int32_t);
                break;
            case ValueType::BOOLEAN:
                size += sizeof(std::uint8_t);
                break;
            case ValueType::VARCHAR:
                size += sizeof(std::uint32_t) + col.max_length;
                break;
        }
    }

    return size;
}

Tuple::Tuple(const Schema& schema) {
    values_.reserve(schema.column_count());
    for (std::size_t i = 0; i < schema.column_count(); ++i) {
        values_.emplace_back();
    }
}

void Tuple::set_value(std::size_t index, const Value& value) {
    values_.at(index) = value;
}

const Value& Tuple::get_value(std::size_t index) const {
    return values_.at(index);
}

std::size_t Tuple::value_count() const {
    return values_.size();
}

std::size_t Tuple::serialized_size() const {
    std::size_t size = 0;

    for (const auto& value : values_) {
        size += value.serialized_size();
    }

    return size;
}

bool Tuple::serialize(
    std::byte* buffer,
    std::size_t buffer_size
) const {
    std::size_t offset = 0;

    for (const auto& value : values_) {
        if (offset + value.serialized_size() > buffer_size) {
            return false;
        }

        std::uint8_t type_byte = static_cast<std::uint8_t>(value.type());
        std::uint8_t null_byte = value.is_null() ? 1 : 0;

        std::memcpy(buffer + offset, &type_byte, sizeof(std::uint8_t));
        offset += sizeof(std::uint8_t);

        std::memcpy(buffer + offset, &null_byte, sizeof(std::uint8_t));
        offset += sizeof(std::uint8_t);

        if (value.is_null()) {
            continue;
        }

        switch (value.type()) {
            case ValueType::INTEGER: {
                const auto int_val = *value.as_int();
                std::memcpy(buffer + offset, &int_val, sizeof(std::int32_t));
                offset += sizeof(std::int32_t);
                break;
            }
            case ValueType::BOOLEAN: {
                const std::uint8_t bool_byte = *value.as_bool() ? 1 : 0;
                std::memcpy(buffer + offset, &bool_byte, sizeof(std::uint8_t));
                offset += sizeof(std::uint8_t);
                break;
            }
            case ValueType::VARCHAR: {
                const auto& str = *value.as_string();
                const std::uint32_t str_len =
                    static_cast<std::uint32_t>(str.size());

                std::memcpy(buffer + offset, &str_len, sizeof(std::uint32_t));
                offset += sizeof(std::uint32_t);

                std::memcpy(buffer + offset, str.data(), str.size());
                offset += str.size();
                break;
            }
        }
    }

    return true;
}

bool Tuple::deserialize(
    const std::byte* buffer,
    std::size_t buffer_size,
    const Schema& schema
) {
    values_.clear();
    values_.reserve(schema.column_count());

    std::size_t offset = 0;

    for (std::size_t i = 0; i < schema.column_count(); ++i) {
        if (offset + 2 > buffer_size) {
            return false;
        }

        std::uint8_t type_byte = 0;
        std::uint8_t null_byte = 0;

        std::memcpy(&type_byte, buffer + offset, sizeof(std::uint8_t));
        offset += sizeof(std::uint8_t);

        std::memcpy(&null_byte, buffer + offset, sizeof(std::uint8_t));
        offset += sizeof(std::uint8_t);

        const ValueType type = static_cast<ValueType>(type_byte);

        if (null_byte != 0) {
            values_.emplace_back();
            continue;
        }

        switch (type) {
            case ValueType::INTEGER: {
                if (offset + sizeof(std::int32_t) > buffer_size) {
                    return false;
                }

                std::int32_t int_val = 0;
                std::memcpy(&int_val, buffer + offset, sizeof(std::int32_t));
                offset += sizeof(std::int32_t);

                values_.emplace_back(int_val);
                break;
            }
            case ValueType::BOOLEAN: {
                if (offset + sizeof(std::uint8_t) > buffer_size) {
                    return false;
                }

                std::uint8_t bool_byte = 0;
                std::memcpy(&bool_byte, buffer + offset, sizeof(std::uint8_t));
                offset += sizeof(std::uint8_t);

                values_.emplace_back(bool_byte != 0);
                break;
            }
            case ValueType::VARCHAR: {
                if (offset + sizeof(std::uint32_t) > buffer_size) {
                    return false;
                }

                std::uint32_t str_len = 0;
                std::memcpy(&str_len, buffer + offset, sizeof(std::uint32_t));
                offset += sizeof(std::uint32_t);

                if (offset + str_len > buffer_size) {
                    return false;
                }

                std::string str(str_len, '\0');
                std::memcpy(str.data(), buffer + offset, str_len);
                offset += str_len;

                values_.emplace_back(str);
                break;
            }
        }
    }

    return true;
}

}  // namespace forgedb::storage
