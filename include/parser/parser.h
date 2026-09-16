#pragma once

#include <optional>
#include <string>
#include <vector>

#include "parser/ast.h"
#include "parser/token.h"

namespace forgedb::parser {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    std::optional<StatementPtr> parse();

    const std::string& error() const;

private:
    bool is_at_end() const;
    const Token& peek() const;
    const Token& previous() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(const std::vector<TokenType>& types);

    void error(const std::string& message);

    StatementPtr statement();
    StatementPtr create_table_statement();
    StatementPtr create_index_statement();
    StatementPtr select_statement();
    StatementPtr insert_statement();

    ExpressionPtr expression();
    ExpressionPtr or_expression();
    ExpressionPtr and_expression();
    ExpressionPtr equality_expression();
    ExpressionPtr comparison_expression();
    ExpressionPtr additive_expression();
    ExpressionPtr multiplicative_expression();
    ExpressionPtr unary_expression();
    ExpressionPtr primary_expression();

    storage::ValueType parse_type();

    std::vector<Token> tokens_;
    std::size_t current_;
    std::string error_message_;
};

}  // namespace forgedb::parser

