#pragma once

#include <cstddef>
#include <string>

namespace forgedb::parser {

enum class TokenType {
    // Keywords
    SELECT,
    FROM,
    WHERE,
    INSERT,
    INTO,
    VALUES,
    CREATE,
    TABLE,
    INDEX,
    ON,
    INTEGER,
    VARCHAR,
    BOOLEAN,
    NULL_KEYWORD,
    AND,
    OR,
    NOT,

    // Literals
    IDENTIFIER,
    NUMBER,
    STRING,

    // Operators
    EQUALS,
    NOT_EQUALS,
    LESS_THAN,
    GREATER_THAN,
    LESS_OR_EQUAL,
    GREATER_OR_EQUAL,
    PLUS,
    MINUS,
    ASTERISK,
    SLASH,

    // Delimiters
    LEFT_PAREN,
    RIGHT_PAREN,
    COMMA,
    SEMICOLON,

    // Special
    END_OF_FILE,
    INVALID
};

struct Token {
    TokenType type;
    std::string lexeme;
    std::size_t line;
    std::size_t column;

    Token(
        TokenType type_,
        const std::string& lexeme_,
        std::size_t line_,
        std::size_t column_
    )
        : type(type_),
          lexeme(lexeme_),
          line(line_),
          column(column_) {}
};

}  // namespace forgedb::parser

