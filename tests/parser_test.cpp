#include <gtest/gtest.h>

#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"

namespace {

using forgedb::parser::BinaryOpExpression;
using forgedb::parser::BinaryOperator;
using forgedb::parser::ColumnRefExpression;
using forgedb::parser::CreateIndexStatement;
using forgedb::parser::CreateTableStatement;
using forgedb::parser::ExpressionType;
using forgedb::parser::InsertStatement;
using forgedb::parser::Lexer;
using forgedb::parser::LiteralExpression;
using forgedb::parser::Parser;
using forgedb::parser::SelectStatement;
using forgedb::parser::StatementType;
using forgedb::parser::TokenType;
using forgedb::storage::ValueType;

TEST(LexerTest, TokenizesKeywords) {
    Lexer lexer("SELECT FROM WHERE CREATE TABLE");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::FROM);
    EXPECT_EQ(tokens[2].type, TokenType::WHERE);
    EXPECT_EQ(tokens[3].type, TokenType::CREATE);
    EXPECT_EQ(tokens[4].type, TokenType::TABLE);
    EXPECT_EQ(tokens[5].type, TokenType::END_OF_FILE);
}

TEST(LexerTest, TokenizesIdentifiers) {
    Lexer lexer("users table_name _column");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].lexeme, "users");
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme, "table_name");
    EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme, "_column");
}

TEST(LexerTest, TokenizesNumbers) {
    Lexer lexer("123 456 0");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[0].lexeme, "123");
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].lexeme, "456");
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[2].lexeme, "0");
}

TEST(LexerTest, TokenizesStrings) {
    Lexer lexer("'hello' \"world\"");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::STRING);
    EXPECT_EQ(tokens[0].lexeme, "hello");
    EXPECT_EQ(tokens[1].type, TokenType::STRING);
    EXPECT_EQ(tokens[1].lexeme, "world");
}

TEST(LexerTest, TokenizesOperators) {
    Lexer lexer("= <> != < > <= >= + - * /");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 12);
    EXPECT_EQ(tokens[0].type, TokenType::EQUALS);
    EXPECT_EQ(tokens[1].type, TokenType::NOT_EQUALS);
    EXPECT_EQ(tokens[2].type, TokenType::NOT_EQUALS);
    EXPECT_EQ(tokens[3].type, TokenType::LESS_THAN);
    EXPECT_EQ(tokens[4].type, TokenType::GREATER_THAN);
    EXPECT_EQ(tokens[5].type, TokenType::LESS_OR_EQUAL);
    EXPECT_EQ(tokens[6].type, TokenType::GREATER_OR_EQUAL);
    EXPECT_EQ(tokens[7].type, TokenType::PLUS);
    EXPECT_EQ(tokens[8].type, TokenType::MINUS);
    EXPECT_EQ(tokens[9].type, TokenType::ASTERISK);
    EXPECT_EQ(tokens[10].type, TokenType::SLASH);
}

TEST(LexerTest, TokenizesDelimiters) {
    Lexer lexer("( ) , ;");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::LEFT_PAREN);
    EXPECT_EQ(tokens[1].type, TokenType::RIGHT_PAREN);
    EXPECT_EQ(tokens[2].type, TokenType::COMMA);
    EXPECT_EQ(tokens[3].type, TokenType::SEMICOLON);
}

TEST(LexerTest, SkipsWhitespace) {
    Lexer lexer("  SELECT\t\nFROM  \r\n  WHERE  ");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::FROM);
    EXPECT_EQ(tokens[2].type, TokenType::WHERE);
}

TEST(LexerTest, SkipsLineComments) {
    Lexer lexer("SELECT -- this is a comment\nFROM");

    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::FROM);
}

TEST(ParserTest, ParsesCreateTable) {
    Lexer lexer("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);
    EXPECT_EQ(stmt.value()->type, StatementType::CREATE_TABLE);

    auto* create_table =
        dynamic_cast<CreateTableStatement*>(stmt.value().get());

    ASSERT_NE(create_table, nullptr);
    EXPECT_EQ(create_table->table_name, "users");
    ASSERT_EQ(create_table->columns.size(), 2);
    EXPECT_EQ(create_table->columns[0].column_name, "id");
    EXPECT_EQ(create_table->columns[0].type, ValueType::INTEGER);
    EXPECT_EQ(create_table->columns[1].column_name, "name");
    EXPECT_EQ(create_table->columns[1].type, ValueType::VARCHAR);
    EXPECT_EQ(create_table->columns[1].max_length, 50);
}

TEST(ParserTest, ParsesCreateIndex) {
    Lexer lexer("CREATE INDEX idx_id ON users (id)");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);
    EXPECT_EQ(stmt.value()->type, StatementType::CREATE_INDEX);

    auto* create_index =
        dynamic_cast<CreateIndexStatement*>(stmt.value().get());

    ASSERT_NE(create_index, nullptr);
    EXPECT_EQ(create_index->index_name, "idx_id");
    EXPECT_EQ(create_index->table_name, "users");
    EXPECT_EQ(create_index->column_name, "id");
}

TEST(ParserTest, ParsesSelectAll) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);
    EXPECT_EQ(stmt.value()->type, StatementType::SELECT);

    auto* select = dynamic_cast<SelectStatement*>(stmt.value().get());

    ASSERT_NE(select, nullptr);
    EXPECT_TRUE(select->columns.empty());
    EXPECT_EQ(select->table_name, "users");
    EXPECT_EQ(select->where_clause, nullptr);
}

TEST(ParserTest, ParsesSelectColumns) {
    Lexer lexer("SELECT id, name FROM users");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.value().get());

    ASSERT_NE(select, nullptr);
    ASSERT_EQ(select->columns.size(), 2);
    EXPECT_EQ(select->columns[0], "id");
    EXPECT_EQ(select->columns[1], "name");
    EXPECT_EQ(select->table_name, "users");
}

TEST(ParserTest, ParsesSelectWithWhere) {
    Lexer lexer("SELECT * FROM users WHERE id = 10");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);

    auto* select = dynamic_cast<SelectStatement*>(stmt.value().get());

    ASSERT_NE(select, nullptr);
    EXPECT_EQ(select->table_name, "users");
    ASSERT_NE(select->where_clause, nullptr);
    EXPECT_EQ(select->where_clause->type, ExpressionType::BINARY_OP);

    auto* binary_op =
        dynamic_cast<BinaryOpExpression*>(select->where_clause.get());

    ASSERT_NE(binary_op, nullptr);
    EXPECT_EQ(binary_op->op, BinaryOperator::EQUALS);

    auto* left = dynamic_cast<ColumnRefExpression*>(binary_op->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->column_name, "id");

    auto* right =
        dynamic_cast<LiteralExpression*>(binary_op->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(*right->value.as_int(), 10);
}

TEST(ParserTest, ParsesInsertWithValues) {
    Lexer lexer("INSERT INTO users VALUES (1, 'Alice')");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);
    EXPECT_EQ(stmt.value()->type, StatementType::INSERT);

    auto* insert = dynamic_cast<InsertStatement*>(stmt.value().get());

    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table_name, "users");
    EXPECT_TRUE(insert->columns.empty());
    ASSERT_EQ(insert->values.size(), 2);

    auto* val1 = dynamic_cast<LiteralExpression*>(insert->values[0].get());
    ASSERT_NE(val1, nullptr);
    EXPECT_EQ(*val1->value.as_int(), 1);

    auto* val2 = dynamic_cast<LiteralExpression*>(insert->values[1].get());
    ASSERT_NE(val2, nullptr);
    EXPECT_EQ(*val2->value.as_string(), "Alice");
}

TEST(ParserTest, ParsesInsertWithColumns) {
    Lexer lexer("INSERT INTO users (id, name) VALUES (1, 'Bob')");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());
    ASSERT_NE(stmt.value(), nullptr);

    auto* insert = dynamic_cast<InsertStatement*>(stmt.value().get());

    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table_name, "users");
    ASSERT_EQ(insert->columns.size(), 2);
    EXPECT_EQ(insert->columns[0], "id");
    EXPECT_EQ(insert->columns[1], "name");
    ASSERT_EQ(insert->values.size(), 2);
}

TEST(ParserTest, ParsesComplexWhereClause) {
    Lexer lexer("SELECT * FROM users WHERE id > 10 AND name = 'John'");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());

    auto* select = dynamic_cast<SelectStatement*>(stmt.value().get());

    ASSERT_NE(select, nullptr);
    ASSERT_NE(select->where_clause, nullptr);

    auto* and_expr =
        dynamic_cast<BinaryOpExpression*>(select->where_clause.get());

    ASSERT_NE(and_expr, nullptr);
    EXPECT_EQ(and_expr->op, BinaryOperator::AND);
}

TEST(ParserTest, HandlesInvalidSyntax) {
    Lexer lexer("SELECT FROM");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_FALSE(stmt.has_value());
    EXPECT_FALSE(parser.error().empty());
}

TEST(ParserTest, ParsesMultipleColumnTypes) {
    Lexer lexer(
        "CREATE TABLE test (a INTEGER, b VARCHAR(100), c BOOLEAN)"
    );
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());

    auto* create_table =
        dynamic_cast<CreateTableStatement*>(stmt.value().get());

    ASSERT_NE(create_table, nullptr);
    ASSERT_EQ(create_table->columns.size(), 3);
    EXPECT_EQ(create_table->columns[0].type, ValueType::INTEGER);
    EXPECT_EQ(create_table->columns[1].type, ValueType::VARCHAR);
    EXPECT_EQ(create_table->columns[1].max_length, 100);
    EXPECT_EQ(create_table->columns[2].type, ValueType::BOOLEAN);
}

}  // namespace

