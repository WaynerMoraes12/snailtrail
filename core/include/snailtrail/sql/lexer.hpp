#pragma once

#include <string_view>
#include <vector>

#include "snailtrail/sql/token.hpp"

namespace snailtrail::sql {

class Lexer {
public:
    explicit Lexer(std::string_view source) noexcept : src_(source) {}

    Token next() noexcept;

    static std::vector<Token> tokenize(std::string_view source);
    static void tokenize_into(std::string_view source, std::vector<Token>& out);

private:
    void skip_trivia() noexcept;
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept;
    Token make(TokenKind kind, std::size_t start) noexcept;

    Token lex_word(std::size_t start) noexcept;
    Token lex_number(std::size_t start) noexcept;
    Token lex_quoted(std::size_t start, char quote, TokenKind kind) noexcept;
    Token lex_variable(std::size_t start) noexcept;
    Token lex_operator(std::size_t start) noexcept;

    std::string_view src_;
    std::size_t pos_ = 0;
    TokenKind previous_ = TokenKind::End;
    int executable_comments_ = 0;
};

}
