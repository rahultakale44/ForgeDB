#pragma once

#include <memory>
#include <string>
#include <vector>

#include "storage/tuple.h"

namespace forgedb::parser {

enum class StatementType {
    SELECT,
    INSERT,
    CREATE_TABLE,
    CREATE_INDEX
};

enum class ExpressionType {
    LITERAL,
    COLUMN_REF,
    BINARY_OP,
    UNARY_OP
};

enum class BinaryOperator {
    EQUALS,
    NOT_EQUALS,
    LESS_THAN,
    GREATER_THAN,
    LESS_OR_EQUAL,
    GREATER_OR_EQUAL,
    AND,
    OR,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE
};

enum class UnaryOperator {
    NOT,
    MINUS
};

// Forward declarations
struct Expression;
struct Statement;

using ExpressionPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;

struct Expression {
    ExpressionType type;

    explicit Expression(ExpressionType type_) : type(type_) {}
    virtual ~Expression() = default;
};

struct LiteralExpression : Expression {
    storage::Value value;

    explicit LiteralExpression(const storage::Value& val)
        : Expression(ExpressionType::LITERAL),
          value(val) {}
};

struct ColumnRefExpression : Expression {
    std::string column_name;
    std::string table_name;  // Optional

    explicit ColumnRefExpression(const std::string& col_name)
        : Expression(ExpressionType::COLUMN_REF),
          column_name(col_name) {}
};

struct BinaryOpExpression : Expression {
    BinaryOperator op;
    ExpressionPtr left;
    ExpressionPtr right;

    BinaryOpExpression(
        BinaryOperator op_,
        ExpressionPtr left_,
        ExpressionPtr right_
    )
        : Expression(ExpressionType::BINARY_OP),
          op(op_),
          left(std::move(left_)),
          right(std::move(right_)) {}
};

struct UnaryOpExpression : Expression {
    UnaryOperator op;
    ExpressionPtr operand;

    UnaryOpExpression(UnaryOperator op_, ExpressionPtr operand_)
        : Expression(ExpressionType::UNARY_OP),
          op(op_),
          operand(std::move(operand_)) {}
};

struct Statement {
    StatementType type;

    explicit Statement(StatementType type_) : type(type_) {}
    virtual ~Statement() = default;
};

struct ColumnDefinitionStmt {
    std::string column_name;
    storage::ValueType type;
    std::size_t max_length;

    ColumnDefinitionStmt(
        const std::string& name,
        storage::ValueType t,
        std::size_t len = 0
    )
        : column_name(name),
          type(t),
          max_length(len) {}
};

struct CreateTableStatement : Statement {
    std::string table_name;
    std::vector<ColumnDefinitionStmt> columns;

    explicit CreateTableStatement(const std::string& name)
        : Statement(StatementType::CREATE_TABLE),
          table_name(name) {}
};

struct CreateIndexStatement : Statement {
    std::string index_name;
    std::string table_name;
    std::string column_name;

    CreateIndexStatement(
        const std::string& idx_name,
        const std::string& tbl_name,
        const std::string& col_name
    )
        : Statement(StatementType::CREATE_INDEX),
          index_name(idx_name),
          table_name(tbl_name),
          column_name(col_name) {}
};

struct SelectStatement : Statement {
    std::vector<std::string> columns;  // Empty means SELECT *
    std::string table_name;
    ExpressionPtr where_clause;  // nullptr means no WHERE

    explicit SelectStatement(const std::string& table)
        : Statement(StatementType::SELECT),
          table_name(table),
          where_clause(nullptr) {}
};

struct InsertStatement : Statement {
    std::string table_name;
    std::vector<std::string> columns;  // Empty means all columns
    std::vector<ExpressionPtr> values;

    explicit InsertStatement(const std::string& table)
        : Statement(StatementType::INSERT),
          table_name(table) {}
};

}  // namespace forgedb::parser

