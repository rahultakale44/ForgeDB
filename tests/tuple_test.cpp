#include <gtest/gtest.h>

#include "storage/tuple.h"

namespace {

using forgedb::storage::Schema;
using forgedb::storage::Tuple;
using forgedb::storage::Value;
using forgedb::storage::ValueType;

TEST(ValueTest, CreatesIntegerValue) {
    Value value(42);

    EXPECT_EQ(value.type(), ValueType::INTEGER);
    EXPECT_FALSE(value.is_null());

    EXPECT_TRUE(value.as_int().has_value());
    EXPECT_EQ(*value.as_int(), 42);

    EXPECT_FALSE(value.as_bool().has_value());
    EXPECT_FALSE(value.as_string().has_value());
}

TEST(ValueTest, CreatesBooleanValue) {
    Value value(true);

    EXPECT_EQ(value.type(), ValueType::BOOLEAN);
    EXPECT_FALSE(value.is_null());

    EXPECT_TRUE(value.as_bool().has_value());
    EXPECT_EQ(*value.as_bool(), true);

    EXPECT_FALSE(value.as_int().has_value());
    EXPECT_FALSE(value.as_string().has_value());
}

TEST(ValueTest, CreatesStringValue) {
    Value value(std::string{"hello"});

    EXPECT_EQ(value.type(), ValueType::VARCHAR);
    EXPECT_FALSE(value.is_null());

    EXPECT_TRUE(value.as_string().has_value());
    EXPECT_EQ(*value.as_string(), "hello");

    EXPECT_FALSE(value.as_int().has_value());
    EXPECT_FALSE(value.as_bool().has_value());
}

TEST(ValueTest, CreatesNullValue) {
    Value value;

    EXPECT_TRUE(value.is_null());
    EXPECT_FALSE(value.as_int().has_value());
    EXPECT_FALSE(value.as_bool().has_value());
    EXPECT_FALSE(value.as_string().has_value());
}

TEST(ValueTest, HandlesNegativeIntegers) {
    Value value(-100);

    EXPECT_EQ(value.type(), ValueType::INTEGER);
    EXPECT_TRUE(value.as_int().has_value());
    EXPECT_EQ(*value.as_int(), -100);
}

TEST(ValueTest, HandlesEmptyString) {
    Value value(std::string{""});

    EXPECT_EQ(value.type(), ValueType::VARCHAR);
    EXPECT_TRUE(value.as_string().has_value());
    EXPECT_EQ(*value.as_string(), "");
}

TEST(ValueTest, EqualityComparison) {
    Value v1(42);
    Value v2(42);
    Value v3(100);

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
}

TEST(SchemaTest, CreatesEmptySchema) {
    Schema schema;

    EXPECT_EQ(schema.column_count(), 0);
}

TEST(SchemaTest, AddsColumns) {
    Schema schema;

    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::BOOLEAN);
    schema.add_column(ValueType::VARCHAR, 50);

    EXPECT_EQ(schema.column_count(), 3);

    EXPECT_EQ(schema.column_type(0), ValueType::INTEGER);
    EXPECT_EQ(schema.column_type(1), ValueType::BOOLEAN);
    EXPECT_EQ(schema.column_type(2), ValueType::VARCHAR);

    EXPECT_EQ(schema.column_max_length(2), 50);
}

TEST(SchemaTest, CalculatesFixedTupleSize) {
    Schema schema;

    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::BOOLEAN);

    const std::size_t expected_size =
        2 * (sizeof(std::uint8_t) + sizeof(std::uint8_t)) +
        sizeof(std::int32_t) +
        sizeof(std::uint8_t);

    EXPECT_EQ(schema.fixed_tuple_size(), expected_size);
}

TEST(TupleTest, CreatesEmptyTuple) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::BOOLEAN);

    Tuple tuple(schema);

    EXPECT_EQ(tuple.value_count(), 2);
    EXPECT_TRUE(tuple.get_value(0).is_null());
    EXPECT_TRUE(tuple.get_value(1).is_null());
}

TEST(TupleTest, SetsAndGetsValues) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    Tuple tuple(schema);

    tuple.set_value(0, Value(42));
    tuple.set_value(1, Value(std::string{"test"}));

    EXPECT_FALSE(tuple.get_value(0).is_null());
    EXPECT_FALSE(tuple.get_value(1).is_null());

    EXPECT_EQ(*tuple.get_value(0).as_int(), 42);
    EXPECT_EQ(*tuple.get_value(1).as_string(), "test");
}

TEST(TupleTest, SerializesAndDeserializes) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::BOOLEAN);
    schema.add_column(ValueType::VARCHAR, 50);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(100));
    tuple1.set_value(1, Value(true));
    tuple1.set_value(2, Value(std::string{"hello"}));

    std::vector<std::byte> buffer(tuple1.serialized_size());

    EXPECT_TRUE(tuple1.serialize(buffer.data(), buffer.size()));

    Tuple tuple2(schema);

    EXPECT_TRUE(tuple2.deserialize(buffer.data(), buffer.size(), schema));

    EXPECT_EQ(*tuple2.get_value(0).as_int(), 100);
    EXPECT_EQ(*tuple2.get_value(1).as_bool(), true);
    EXPECT_EQ(*tuple2.get_value(2).as_string(), "hello");
}

TEST(TupleTest, SerializesNullValues) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::VARCHAR, 50);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(42));

    std::vector<std::byte> buffer(tuple1.serialized_size());

    EXPECT_TRUE(tuple1.serialize(buffer.data(), buffer.size()));

    Tuple tuple2(schema);

    EXPECT_TRUE(tuple2.deserialize(buffer.data(), buffer.size(), schema));

    EXPECT_EQ(*tuple2.get_value(0).as_int(), 42);
    EXPECT_TRUE(tuple2.get_value(1).is_null());
}

TEST(TupleTest, HandlesMultipleValues) {
    Schema schema;

    for (int i = 0; i < 10; ++i) {
        schema.add_column(ValueType::INTEGER);
    }

    Tuple tuple1(schema);

    for (std::size_t i = 0; i < 10; ++i) {
        tuple1.set_value(i, Value(static_cast<std::int32_t>(i * 10)));
    }

    std::vector<std::byte> buffer(tuple1.serialized_size());

    EXPECT_TRUE(tuple1.serialize(buffer.data(), buffer.size()));

    Tuple tuple2(schema);

    EXPECT_TRUE(tuple2.deserialize(buffer.data(), buffer.size(), schema));

    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(
            *tuple2.get_value(i).as_int(),
            static_cast<std::int32_t>(i * 10)
        );
    }
}

TEST(TupleTest, HandlesMixedNullAndNonNull) {
    Schema schema;
    schema.add_column(ValueType::INTEGER);
    schema.add_column(ValueType::BOOLEAN);
    schema.add_column(ValueType::VARCHAR, 20);
    schema.add_column(ValueType::INTEGER);

    Tuple tuple1(schema);
    tuple1.set_value(0, Value(1));
    tuple1.set_value(2, Value(std::string{"data"}));

    std::vector<std::byte> buffer(tuple1.serialized_size());

    EXPECT_TRUE(tuple1.serialize(buffer.data(), buffer.size()));

    Tuple tuple2(schema);

    EXPECT_TRUE(tuple2.deserialize(buffer.data(), buffer.size(), schema));

    EXPECT_EQ(*tuple2.get_value(0).as_int(), 1);
    EXPECT_TRUE(tuple2.get_value(1).is_null());
    EXPECT_EQ(*tuple2.get_value(2).as_string(), "data");
    EXPECT_TRUE(tuple2.get_value(3).is_null());
}

}  // namespace
