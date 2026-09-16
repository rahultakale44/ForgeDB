#include "parser/lexer.h"

#include <cctype>

namespace forgedb::parser {

Lexer::Lexer(const std::string& source)
    : source_(source),
      current_(0),
      line_(1),
      column_(1),
      token_start_column_(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!is_at_end()) {
        skip_whitespace();

        if (is_at_end()) {
            break;
        }

        token_start_column_ = column_;

        Token token = scan_token();

        if (token.type != TokenType::INVALID) {
            tokens.push_back(token);
        }
    }

    tokens.emplace_back(
        TokenType::END_OF_FILE,
        "",
        line_,
        column_
    );

    return tokens;
}

bool Lexer::is_at_end() const {
    return current_ >= source_.size();
}

char Lexer::advance() {
    if (is_at_end()) {
        return '\0';
    }

    char c = source_[current_++];

    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }

    return c;
}

char Lexer::peek() const {
    if (is_at_end()) {
        return '\0';
    }

    return source_[current_];
}

char Lexer::peek_next() const {
    if (current_ + 1 >= source_.size()) {
        return '\0';
    }

    return source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (is_at_end() || source_[current_] != expected) {
        return false;
    }

    advance();
    return true;
}

void Lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek();

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '-' && peek_next() == '-') {
            skip_line_comment();
        } else {
            break;
        }
    }
}

void Lexer::skip_line_comment() {
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
}

Token Lexer::scan_token() {
    char c = advance();

    if (std::isalpha(c) || c == '_') {
        current_--;
        column_--;
        return identifier();
    }

    if (std::isdigit(c)) {
        current_--;
        column_--;
        return number();
    }

    switch (c) {
        case '(':
            return Token(
                TokenType::LEFT_PAREN,
                "(",
                line_,
                token_start_column_
            );
        case ')':
            return Token(
                TokenType::RIGHT_PAREN,
                ")",
                line_,
                token_start_column_
            );
        case ',':
            return Token(
                TokenType::COMMA,
                ",",
                line_,
                token_start_column_
            );
        case ';':
            return Token(
                TokenType::SEMICOLON,
                ";",
                line_,
                token_start_column_
            );
        case '+':
            return Token(
                TokenType::PLUS,
                "+",
                line_,
                token_start_column_
            );
        case '-':
            return Token(
                TokenType::MINUS,
                "-",
                line_,
                token_start_column_
            );
        case '*':
            return Token(
                TokenType::ASTERISK,
                "*",
                line_,
                token_start_column_
            );
        case '/':
            return Token(
                TokenType::SLASH,
                "/",
                line_,
                token_start_column_
            );
        case '=':
            return Token(
                TokenType::EQUALS,
                "=",
                line_,
                token_start_column_
            );
        case '<':
            if (match('=')) {
                return Token(
                    TokenType::LESS_OR_EQUAL,
                    "<=",
                    line_,
                    token_start_column_
                );
            } else if (match('>')) {
                return Token(
                    TokenType::NOT_EQUALS,
                    "<>",
                    line_,
                    token_start_column_
                );
            }
            return Token(
                TokenType::LESS_THAN,
                "<",
                line_,
                token_start_column_
            );
        case '>':
            if (match('=')) {
                return Token(
                    TokenType::GREATER_OR_EQUAL,
                    ">=",
                    line_,
                    token_start_column_
                );
            }
            return Token(
                TokenType::GREATER_THAN,
                ">",
                line_,
                token_start_column_
            );
        case '!':
            if (match('=')) {
                return Token(
                    TokenType::NOT_EQUALS,
                    "!=",
                    line_,
                    token_start_column_
                );
            }
            break;
        case '\'':
        case '"':
            current_--;
            column_--;
            return string();
    }

    return Token(
        TokenType::INVALID,
        std::string(1, c),
        line_,
        token_start_column_
    );
}

Token Lexer::identifier() {
    std::size_t start = current_;

    while (!is_at_end() &&
           (std::isalnum(peek()) || peek() == '_')) {
        advance();
    }

    std::string text = source_.substr(start, current_ - start);

    auto keywords_map = keywords();
    auto it = keywords_map.find(text);

    if (it != keywords_map.end()) {
        return Token(it->second, text, line_, token_start_column_);
    }

    return Token(
        TokenType::IDENTIFIER,
        text,
        line_,
        token_start_column_
    );
}

Token Lexer::number() {
    std::size_t start = current_;

    while (!is_at_end() && std::isdigit(peek())) {
        advance();
    }

    std::string text = source_.substr(start, current_ - start);

    return Token(
        TokenType::NUMBER,
        text,
        line_,
        token_start_column_
    );
}

Token Lexer::string() {
    char quote = advance();
    std::size_t start = current_;

    while (!is_at_end() && peek() != quote) {
        advance();
    }

    if (is_at_end()) {
        return Token(
            TokenType::INVALID,
            "Unterminated string",
            line_,
            token_start_column_
        );
    }

    std::string text = source_.substr(start, current_ - start);

    advance();  // Closing quote

    return Token(
        TokenType::STRING,
        text,
        line_,
        token_start_column_
    );
}

std::unordered_map<std::string, TokenType> Lexer::keywords() {
    static std::unordered_map<std::string, TokenType> kw = {
        {"SELECT", TokenType::SELECT},
        {"FROM", TokenType::FROM},
        {"WHERE", TokenType::WHERE},
        {"INSERT", TokenType::INSERT},
        {"INTO", TokenType::INTO},
        {"VALUES", TokenType::VALUES},
        {"CREATE", TokenType::CREATE},
        {"TABLE", TokenType::TABLE},
        {"INDEX", TokenType::INDEX},
        {"ON", TokenType::ON},
        {"INTEGER", TokenType::INTEGER},
        {"VARCHAR", TokenType::VARCHAR},
        {"BOOLEAN", TokenType::BOOLEAN},
        {"NULL", TokenType::NULL_KEYWORD},
        {"AND", TokenType::AND},
        {"OR", TokenType::OR},
        {"NOT", TokenType::NOT}
    };

    return kw;
}

}  // namespace forgedb::parser

