#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace snailtrail::sql {

enum class TokenKind : std::uint8_t {
    Word,
    QuotedIdentifier,
    String,
    Number,
    HexNumber,
    Placeholder,
    Variable,
    Operator,
    LParen,
    RParen,
    Comma,
    Dot,
    Semicolon,
    End,
};

std::string_view token_kind_name(TokenKind kind) noexcept;

struct Token {
    TokenKind kind = TokenKind::End;
    std::string_view text;
    std::size_t offset = 0;

    [[nodiscard]] bool is(TokenKind k) const noexcept { return kind == k; }

    [[nodiscard]] bool is_word(std::string_view word) const noexcept;

    [[nodiscard]] bool is_op(std::string_view op) const noexcept {
        return kind == TokenKind::Operator && text == op;
    }

    [[nodiscard]] bool is_literal() const noexcept {
        return kind == TokenKind::String || kind == TokenKind::Number ||
               kind == TokenKind::HexNumber;
    }

    [[nodiscard]] bool is_identifier() const noexcept {
        return kind == TokenKind::Word || kind == TokenKind::QuotedIdentifier;
    }
};

std::string unquote_identifier(const Token& token);
std::string unquote_string(std::string_view literal);

}
