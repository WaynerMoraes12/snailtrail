#include "snailtrail/sql/fingerprint.hpp"

#include "snailtrail/sql/keywords.hpp"
#include "snailtrail/sql/lexer.hpp"
#include "snailtrail/util/hash.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

std::string Fingerprint::id_hex() const { return util::to_hex(id); }

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);

bool is_value_token(const Token& t) noexcept {
    return t.is_literal() || t.kind == TokenKind::Placeholder || t.is_word("null") ||
           t.is_word("true") || t.is_word("false");
}

bool is_sign(const Token& t) noexcept { return t.is_op("-") || t.is_op("+"); }

bool sign_is_unary(const Token* previous) noexcept {
    if (previous == nullptr) return true;
    switch (previous->kind) {
    case TokenKind::Operator:
    case TokenKind::LParen:
    case TokenKind::Comma: return true;
    case TokenKind::Word: return is_operator_keyword(previous->text);
    default: return false;
    }
}

std::size_t value_list_end(const std::vector<Token>& tokens, std::size_t open) noexcept {
    bool expect_value = true;
    for (std::size_t i = open + 1; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (expect_value) {
            if (is_sign(t) && i + 1 < tokens.size() && tokens[i + 1].is_literal()) {
                ++i;
            } else if (!is_value_token(t)) {
                return npos;
            }
            expect_value = false;
        } else if (t.kind == TokenKind::Comma) {
            expect_value = true;
        } else if (t.kind == TokenKind::RParen) {
            return i;
        } else {
            return npos;
        }
    }
    return npos;
}

std::size_t matching_paren(const std::vector<Token>& tokens, std::size_t open) noexcept {
    int depth = 0;
    for (std::size_t i = open; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::LParen) {
            ++depth;
        } else if (tokens[i].kind == TokenKind::RParen) {
            if (--depth == 0) return i;
        } else if (tokens[i].kind == TokenKind::End) {
            return i - 1;
        }
    }
    return tokens.size() - 1;
}

class FingerprintWriter {
public:
    explicit FingerprintWriter(std::string& out) : out_(out) {}

    void write(const Token& t) {
        space_before(t.kind);
        switch (t.kind) {
        case TokenKind::Word:
        case TokenKind::Variable: append_lower(t.text); break;
        case TokenKind::QuotedIdentifier: append_lower(unquote_identifier(t)); break;
        case TokenKind::String:
        case TokenKind::Number:
        case TokenKind::HexNumber:
        case TokenKind::Placeholder: out_ += '?'; break;
        default: out_ += t.text;
        }
        previous_ = t.kind;
        previous_opens_group_ = t.kind == TokenKind::Word && is_operator_keyword(t.text);
    }

    void write_group(std::string_view group) {
        out_ += group;
        previous_ = TokenKind::RParen;
        previous_opens_group_ = false;
    }

    void write_unit(std::string_view unit) {
        space_before(TokenKind::Word);
        write_group(unit);
    }

private:
    void append_lower(std::string_view s) {
        for (char c : s) out_ += util::to_lower(c);
    }

    void space_before(TokenKind kind) {
        if (out_.empty()) return;
        if (kind == TokenKind::Comma || kind == TokenKind::RParen || kind == TokenKind::Dot ||
            kind == TokenKind::Semicolon) {
            return;
        }
        if (previous_ == TokenKind::LParen || previous_ == TokenKind::Dot) return;
        if (kind == TokenKind::LParen) {
            const bool call_like = (previous_ == TokenKind::Word && !previous_opens_group_) ||
                                   previous_ == TokenKind::QuotedIdentifier;
            if (call_like) return;
        }
        out_ += ' ';
    }

    std::string& out_;
    TokenKind previous_ = TokenKind::End;
    bool previous_opens_group_ = false;
};

}

void Fingerprinter::compute_into(std::string_view sql, Fingerprint& out) {
    Lexer::tokenize_into(sql, tokens_);
    out.text.clear();
    out.kind = classify_statement(tokens_);

    const bool insert_like =
        out.kind == StatementKind::Insert || out.kind == StatementKind::Replace;
    bool values_collapsed = false;
    FingerprintWriter writer(out.text);

    const std::size_t end = tokens_.size() - 1;
    for (std::size_t i = 0; i < end; ++i) {
        const Token& t = tokens_[i];
        const Token* previous = i > 0 ? &tokens_[i - 1] : nullptr;
        const Token& next = tokens_[i + 1];

        if (t.kind == TokenKind::Semicolon) {
            std::size_t j = i + 1;
            while (j < end && tokens_[j].kind == TokenKind::Semicolon) ++j;
            if (j == end) break;
            writer.write(t);
            continue;
        }

        if (t.is_word("in") && next.kind == TokenKind::LParen) {
            const std::size_t close = value_list_end(tokens_, i + 1);
            if (close != npos) {
                writer.write(t);
                writer.write_group("(?+)");
                i = close;
                continue;
            }
        }

        if (insert_like && !values_collapsed && (t.is_word("values") || t.is_word("value")) &&
            next.kind == TokenKind::LParen) {
            std::size_t close = matching_paren(tokens_, i + 1);
            while (close + 2 < end && tokens_[close + 1].kind == TokenKind::Comma &&
                   tokens_[close + 2].kind == TokenKind::LParen) {
                close = matching_paren(tokens_, close + 2);
            }
            writer.write_unit("values(?+)");
            values_collapsed = true;
            i = close;
            continue;
        }

        if (is_sign(t) && next.is_literal() && sign_is_unary(previous)) continue;

        writer.write(t);
    }

    out.id = util::fnv1a_64(out.text);
}

Fingerprint Fingerprinter::compute(std::string_view sql) {
    Fingerprint fp;
    compute_into(sql, fp);
    return fp;
}

Fingerprint fingerprint(std::string_view sql) {
    Fingerprinter f;
    return f.compute(sql);
}

}
