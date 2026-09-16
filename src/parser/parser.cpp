#include "parser/parser.h"

#include <sstream>

namespace forgedb::parser {

Parser::Parser(const std::vector<Token>& tokens)
    : tokens_(tokens),
      current_(0) {}

std::optional<StatementPtr> Parser::parse() {
    if (is_at_end()) {
        error("Empty input");
        return std::nullopt;
    }

    StatementPtr stmt = statement();

    if (!error_message_.empty()) {
        return std::nullopt;
    }

    return stmt;
}

const std::string& Parser::error() const {
    return error_message_;
}

bool Parser::is_at_end() const {
    return current_ >= tokens_.size() ||
           tokens_[current_].type == TokenType::END_OF_FILE;
}

const Token& Parser::peek() const {
    if (current_ < tokens_.size()) {
        return tokens_[current_];
    }

    static Token eof(TokenType::END_OF_FILE, "", 0, 0);
    return eof;
}

const Token& Parser::previous() const {
    if (current_ > 0) {
        return tokens_[current_ - 1];
    }

    return tokens_[0];
}

Token Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }

    return previous();
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) {
        return false;
    }

    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }

    return false;
}

bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (match(type)) {
            return true;
        }
    }

    return false;
}

void Parser::error(const std::string& message) {
    if (!error_message_.empty()) {
        return;
    }

    std::ostringstream oss;
    oss << "Parse error at line " << peek().line
        << ", column " << peek().column << ": " << message;

    error_message_ = oss.str();
}

StatementPtr Parser::statement() {
    if (match(TokenType::CREATE)) {
        if (match(TokenType::TABLE)) {
            return create_table_statement();
        } else if (match(TokenType::INDEX)) {
            return create_index_statement();
        } else {
            error("Expected TABLE or INDEX after CREATE");
            return nullptr;
        }
    }

    if (match(TokenType::SELECT)) {
        return select_statement();
    }

    if (match(TokenType::INSERT)) {
        return insert_statement();
    }

    error("Expected CREATE, SELECT, or INSERT");
    return nullptr;
}

StatementPtr Parser::create_table_statement() {
    if (!check(TokenType::IDENTIFIER)) {
        error("Expected table name");
        return nullptr;
    }

    std::string table_name = advance().lexeme;

    auto stmt = std::make_unique<CreateTableStatement>(table_name);

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after table name");
        return nullptr;
    }

    do {
        if (!check(TokenType::IDENTIFIER)) {
            error("Expected column name");
            return nullptr;
        }

        std::string column_name = advance().lexeme;

        storage::ValueType type = parse_type();

        if (!error_message_.empty()) {
            return nullptr;
        }

        std::size_t max_length = 0;

        if (type == storage::ValueType::VARCHAR) {
            if (!match(TokenType::LEFT_PAREN)) {
                error("Expected '(' after VARCHAR");
                return nullptr;
            }

            if (!check(TokenType::NUMBER)) {
                error("Expected length for VARCHAR");
                return nullptr;
            }

            max_length = std::stoul(advance().lexeme);

            if (!match(TokenType::RIGHT_PAREN)) {
                error("Expected ')' after VARCHAR length");
                return nullptr;
            }
        }

        stmt->columns.emplace_back(column_name, type, max_length);

    } while (match(TokenType::COMMA));

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after column definitions");
        return nullptr;
    }

    return stmt;
}

StatementPtr Parser::create_index_statement() {
    if (!check(TokenType::IDENTIFIER)) {
        error("Expected index name");
        return nullptr;
    }

    std::string index_name = advance().lexeme;

    if (!match(TokenType::ON)) {
        error("Expected ON after index name");
        return nullptr;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error("Expected table name");
        return nullptr;
    }

    std::string table_name = advance().lexeme;

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after table name");
        return nullptr;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error("Expected column name");
        return nullptr;
    }

    std::string column_name = advance().lexeme;

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after column name");
        return nullptr;
    }

    return std::make_unique<CreateIndexStatement>(
        index_name,
        table_name,
        column_name
    );
}

StatementPtr Parser::select_statement() {
    auto stmt = std::make_unique<SelectStatement>("");

    if (match(TokenType::ASTERISK)) {
        // SELECT * - leave columns empty
    } else {
        do {
            if (!check(TokenType::IDENTIFIER)) {
                error("Expected column name");
                return nullptr;
            }

            stmt->columns.push_back(advance().lexeme);

        } while (match(TokenType::COMMA));
    }

    if (!match(TokenType::FROM)) {
        error("Expected FROM in SELECT statement");
        return nullptr;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error("Expected table name");
        return nullptr;
    }

    stmt->table_name = advance().lexeme;

    if (match(TokenType::WHERE)) {
        stmt->where_clause = expression();

        if (!error_message_.empty()) {
            return nullptr;
        }
    }

    return stmt;
}

StatementPtr Parser::insert_statement() {
    if (!match(TokenType::INTO)) {
        error("Expected INTO after INSERT");
        return nullptr;
    }

    if (!check(TokenType::IDENTIFIER)) {
        error("Expected table name");
        return nullptr;
    }

    std::string table_name = advance().lexeme;

    auto stmt = std::make_unique<InsertStatement>(table_name);

    if (match(TokenType::LEFT_PAREN)) {
        do {
            if (!check(TokenType::IDENTIFIER)) {
                error("Expected column name");
                return nullptr;
            }

            stmt->columns.push_back(advance().lexeme);

        } while (match(TokenType::COMMA));

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after column list");
            return nullptr;
        }
    }

    if (!match(TokenType::VALUES)) {
        error("Expected VALUES in INSERT statement");
        return nullptr;
    }

    if (!match(TokenType::LEFT_PAREN)) {
        error("Expected '(' after VALUES");
        return nullptr;
    }

    do {
        ExpressionPtr expr = expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        stmt->values.push_back(std::move(expr));

    } while (match(TokenType::COMMA));

    if (!match(TokenType::RIGHT_PAREN)) {
        error("Expected ')' after values");
        return nullptr;
    }

    return stmt;
}

ExpressionPtr Parser::expression() {
    return or_expression();
}

ExpressionPtr Parser::or_expression() {
    ExpressionPtr left = and_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (match(TokenType::OR)) {
        ExpressionPtr right = and_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            BinaryOperator::OR,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::and_expression() {
    ExpressionPtr left = equality_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (match(TokenType::AND)) {
        ExpressionPtr right = equality_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            BinaryOperator::AND,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::equality_expression() {
    ExpressionPtr left = comparison_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (true) {
        BinaryOperator op;

        if (match(TokenType::EQUALS)) {
            op = BinaryOperator::EQUALS;
        } else if (match(TokenType::NOT_EQUALS)) {
            op = BinaryOperator::NOT_EQUALS;
        } else {
            break;
        }

        ExpressionPtr right = comparison_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            op,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::comparison_expression() {
    ExpressionPtr left = additive_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (true) {
        BinaryOperator op;

        if (match(TokenType::LESS_THAN)) {
            op = BinaryOperator::LESS_THAN;
        } else if (match(TokenType::GREATER_THAN)) {
            op = BinaryOperator::GREATER_THAN;
        } else if (match(TokenType::LESS_OR_EQUAL)) {
            op = BinaryOperator::LESS_OR_EQUAL;
        } else if (match(TokenType::GREATER_OR_EQUAL)) {
            op = BinaryOperator::GREATER_OR_EQUAL;
        } else {
            break;
        }

        ExpressionPtr right = additive_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            op,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::additive_expression() {
    ExpressionPtr left = multiplicative_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (true) {
        BinaryOperator op;

        if (match(TokenType::PLUS)) {
            op = BinaryOperator::PLUS;
        } else if (match(TokenType::MINUS)) {
            op = BinaryOperator::MINUS;
        } else {
            break;
        }

        ExpressionPtr right = multiplicative_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            op,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::multiplicative_expression() {
    ExpressionPtr left = unary_expression();

    if (!error_message_.empty()) {
        return nullptr;
    }

    while (true) {
        BinaryOperator op;

        if (match(TokenType::ASTERISK)) {
            op = BinaryOperator::MULTIPLY;
        } else if (match(TokenType::SLASH)) {
            op = BinaryOperator::DIVIDE;
        } else {
            break;
        }

        ExpressionPtr right = unary_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        left = std::make_unique<BinaryOpExpression>(
            op,
            std::move(left),
            std::move(right)
        );
    }

    return left;
}

ExpressionPtr Parser::unary_expression() {
    if (match(TokenType::NOT)) {
        ExpressionPtr operand = unary_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        return std::make_unique<UnaryOpExpression>(
            UnaryOperator::NOT,
            std::move(operand)
        );
    }

    if (match(TokenType::MINUS)) {
        ExpressionPtr operand = unary_expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        return std::make_unique<UnaryOpExpression>(
            UnaryOperator::MINUS,
            std::move(operand)
        );
    }

    return primary_expression();
}

ExpressionPtr Parser::primary_expression() {
    if (match(TokenType::NUMBER)) {
        const std::string& lexeme = previous().lexeme;
        std::int32_t value = std::stoi(lexeme);

        return std::make_unique<LiteralExpression>(
            storage::Value(value)
        );
    }

    if (match(TokenType::STRING)) {
        const std::string& value = previous().lexeme;

        return std::make_unique<LiteralExpression>(
            storage::Value(value)
        );
    }

    if (match(TokenType::NULL_KEYWORD)) {
        return std::make_unique<LiteralExpression>(
            storage::Value()
        );
    }

    if (match(TokenType::IDENTIFIER)) {
        const std::string& name = previous().lexeme;

        return std::make_unique<ColumnRefExpression>(name);
    }

    if (match(TokenType::LEFT_PAREN)) {
        ExpressionPtr expr = expression();

        if (!error_message_.empty()) {
            return nullptr;
        }

        if (!match(TokenType::RIGHT_PAREN)) {
            error("Expected ')' after expression");
            return nullptr;
        }

        return expr;
    }

    error("Expected expression");
    return nullptr;
}

storage::ValueType Parser::parse_type() {
    if (match(TokenType::INTEGER)) {
        return storage::ValueType::INTEGER;
    }

    if (match(TokenType::VARCHAR)) {
        return storage::ValueType::VARCHAR;
    }

    if (match(TokenType::BOOLEAN)) {
        return storage::ValueType::BOOLEAN;
    }

    error("Expected type name (INTEGER, VARCHAR, or BOOLEAN)");
    return storage::ValueType::INTEGER;
}

}  // namespace forgedb::parser

