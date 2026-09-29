#include "snailtrail/sql/lexer.hpp"

#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

namespace {

constexpr bool is_word_char(char c) noexcept {
    return util::is_alpha(c) || util::is_digit(c) || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

constexpr bool is_word_start(char c) noexcept {
    return util::is_alpha(c) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

constexpr bool is_hex_digit(char c) noexcept {
    return util::is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr bool starts_line_comment(char next) noexcept {
    return next == '\0' || static_cast<unsigned char>(next) <= ' ';
}

}

char Lexer::peek(std::size_t ahead) const noexcept {
    const std::size_t at = pos_ + ahead;
    return at < src_.size() ? src_[at] : '\0';
}

Token Lexer::make(TokenKind kind, std::size_t start) noexcept {
    if (pos_ > src_.size()) pos_ = src_.size();
    return Token{kind, src_.substr(start, pos_ - start), start};
}

void Lexer::skip_trivia() noexcept {
    while (pos_ < src_.size()) {
        const char c = src_[pos_];
        if (util::is_space(c)) {
            ++pos_;
        } else if (c == '#' || (c == '-' && peek(1) == '-' && starts_line_comment(peek(2)))) {
            while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
        } else if (c == '/' && peek(1) == '*') {
            if (peek(2) == '!') {
                pos_ += 3;
                while (pos_ < src_.size() && util::is_digit(src_[pos_])) ++pos_;
                ++executable_comments_;
            } else {
                const std::size_t close = src_.find("*/", pos_ + 2);
                pos_ = close == std::string_view::npos ? src_.size() : close + 2;
            }
        } else if (c == '*' && peek(1) == '/' && executable_comments_ > 0) {
            pos_ += 2;
            --executable_comments_;
        } else {
            break;
        }
    }
}

Token Lexer::next() noexcept {
    skip_trivia();
    if (pos_ >= src_.size()) return Token{TokenKind::End, src_.substr(src_.size()), src_.size()};

    const std::size_t start = pos_;
    const char c = src_[pos_];
    const char n = peek(1);
    Token token;

    if (is_word_start(c)) {
        const char lower = util::to_lower(c);
        if ((lower == 'x' || lower == 'b') && n == '\'') {
            ++pos_;
            token = lex_quoted(start, '\'', TokenKind::HexNumber);
        } else if (lower == 'n' && n == '\'') {
            ++pos_;
            token = lex_quoted(start, '\'', TokenKind::String);
        } else {
            token = lex_word(start);
            if (c == '_' && peek() == '\'') token = lex_quoted(start, '\'', TokenKind::String);
        }
    } else if (util::is_digit(c) ||
               (c == '.' && util::is_digit(n) && previous_ != TokenKind::Word &&
                previous_ != TokenKind::QuotedIdentifier && previous_ != TokenKind::RParen)) {
        token = lex_number(start);
    } else {
        switch (c) {
        case '\'':
        case '"': token = lex_quoted(start, c, TokenKind::String); break;
        case '`': token = lex_quoted(start, '`', TokenKind::QuotedIdentifier); break;
        case '@': token = lex_variable(start); break;
        case '?': ++pos_; token = make(TokenKind::Placeholder, start); break;
        case '(': ++pos_; token = make(TokenKind::LParen, start); break;
        case ')': ++pos_; token = make(TokenKind::RParen, start); break;
        case ',': ++pos_; token = make(TokenKind::Comma, start); break;
        case '.': ++pos_; token = make(TokenKind::Dot, start); break;
        case ';': ++pos_; token = make(TokenKind::Semicolon, start); break;
        default: token = lex_operator(start);
        }
    }
    previous_ = token.kind;
    return token;
}

Token Lexer::lex_word(std::size_t start) noexcept {
    while (pos_ < src_.size() && is_word_char(src_[pos_])) ++pos_;
    return make(TokenKind::Word, start);
}

Token Lexer::lex_number(std::size_t start) noexcept {
    const char lower1 = util::to_lower(peek(1));
    if (src_[pos_] == '0' && lower1 == 'x' && is_hex_digit(peek(2))) {
        pos_ += 2;
        while (pos_ < src_.size() && is_hex_digit(src_[pos_])) ++pos_;
        if (pos_ < src_.size() && is_word_char(src_[pos_])) return lex_word(start);
        return make(TokenKind::HexNumber, start);
    }
    if (src_[pos_] == '0' && lower1 == 'b' && (peek(2) == '0' || peek(2) == '1')) {
        pos_ += 2;
        while (pos_ < src_.size() && (src_[pos_] == '0' || src_[pos_] == '1')) ++pos_;
        if (pos_ < src_.size() && is_word_char(src_[pos_])) return lex_word(start);
        return make(TokenKind::HexNumber, start);
    }

    bool plain_integer = true;
    while (pos_ < src_.size() && util::is_digit(src_[pos_])) ++pos_;
    if (peek() == '.') {
        plain_integer = false;
        ++pos_;
        while (pos_ < src_.size() && util::is_digit(src_[pos_])) ++pos_;
    }
    if (util::to_lower(peek()) == 'e') {
        const char after = peek(1);
        if (util::is_digit(after) || ((after == '+' || after == '-') && util::is_digit(peek(2)))) {
            plain_integer = false;
            pos_ += 2;
            while (pos_ < src_.size() && util::is_digit(src_[pos_])) ++pos_;
        }
    }
    if (plain_integer && pos_ < src_.size() && is_word_char(src_[pos_])) return lex_word(start);
    return make(TokenKind::Number, start);
}

Token Lexer::lex_quoted(std::size_t start, char quote, TokenKind kind) noexcept {
    ++pos_;
    while (pos_ < src_.size()) {
        const char c = src_[pos_];
        if (c == '\\' && quote != '`') {
            pos_ += 2;
        } else if (c == quote) {
            if (peek(1) == quote) {
                pos_ += 2;
            } else {
                ++pos_;
                break;
            }
        } else {
            ++pos_;
        }
    }
    return make(kind, start);
}

Token Lexer::lex_variable(std::size_t start) noexcept {
    ++pos_;
    if (peek() == '@') ++pos_;
    const char c = peek();
    if (c == '\'' || c == '"' || c == '`') return lex_quoted(start, c, TokenKind::Variable);
    while (pos_ < src_.size() && (is_word_char(src_[pos_]) || src_[pos_] == '.')) ++pos_;
    return make(TokenKind::Variable, start);
}

Token Lexer::lex_operator(std::size_t start) noexcept {
    static constexpr std::string_view three[] = {"<=>", "->>"};
    static constexpr std::string_view two[] = {"<=", ">=", "<>", "!=", "||", "&&",
                                               ":=", "->", "<<", ">>"};
    const std::string_view rest = src_.substr(pos_);
    for (std::string_view op : three) {
        if (rest.starts_with(op)) {
            pos_ += 3;
            return make(TokenKind::Operator, start);
        }
    }
    for (std::string_view op : two) {
        if (rest.starts_with(op)) {
            pos_ += 2;
            return make(TokenKind::Operator, start);
        }
    }
    ++pos_;
    return make(TokenKind::Operator, start);
}

std::vector<Token> Lexer::tokenize(std::string_view source) {
    std::vector<Token> tokens;
    tokenize_into(source, tokens);
    return tokens;
}

void Lexer::tokenize_into(std::string_view source, std::vector<Token>& out) {
    out.clear();
    Lexer lexer(source);
    while (true) {
        const Token t = lexer.next();
        out.push_back(t);
        if (t.kind == TokenKind::End) break;
    }
}

}
