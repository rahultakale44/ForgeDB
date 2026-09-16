#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "parser/token.h"

namespace forgedb::parser {

class Lexer {
public:
    explicit Lexer(const std::string& source);

    std::vector<Token> tokenize();

private:
    bool is_at_end() const;
    char advance();
    char peek() const;
    char peek_next() const;
    bool match(char expected);

    void skip_whitespace();
    void skip_line_comment();

    Token scan_token();
    Token identifier();
    Token number();
    Token string();

    static std::unordered_map<std::string, TokenType> keywords();

    std::string source_;
    std::size_t current_;
    std::size_t line_;
    std::size_t column_;
    std::size_t token_start_column_;
};

}  // namespace forgedb::parser

