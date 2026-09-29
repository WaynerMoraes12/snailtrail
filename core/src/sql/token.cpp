#include "snailtrail/sql/token.hpp"

#include <string>

#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

std::string_view token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::Word: return "word";
    case TokenKind::QuotedIdentifier: return "quoted-identifier";
    case TokenKind::String: return "string";
    case TokenKind::Number: return "number";
    case TokenKind::HexNumber: return "hex-number";
    case TokenKind::Placeholder: return "placeholder";
    case TokenKind::Variable: return "variable";
    case TokenKind::Operator: return "operator";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Semicolon: return ";";
    case TokenKind::End: return "end";
    }
    return "?";
}

bool Token::is_word(std::string_view word) const noexcept {
    return kind == TokenKind::Word && util::iequals(text, word);
}

std::string unquote_identifier(const Token& token) {
    if (token.kind != TokenKind::QuotedIdentifier) return std::string(token.text);
    std::string_view body = token.text.substr(1);
    if (!body.empty() && body.back() == '`') body.remove_suffix(1);
    std::string out;
    out.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        out += body[i];
        if (body[i] == '`' && i + 1 < body.size() && body[i + 1] == '`') ++i;
    }
    return out;
}

std::string unquote_string(std::string_view literal) {
    const std::size_t open = literal.find_first_of("'\"");
    if (open == std::string_view::npos) return std::string(literal);
    const char quote = literal[open];
    std::string out;
    out.reserve(literal.size());
    for (std::size_t i = open + 1; i < literal.size(); ++i) {
        const char c = literal[i];
        if (c == '\\' && i + 1 < literal.size()) {
            const char e = literal[++i];
            switch (e) {
            case '0': out += '\0'; break;
            case 'b': out += '\b'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'Z': out += '\x1A'; break;
            case '%':
            case '_':
                out += '\\';
                out += e;
                break;
            default: out += e;
            }
        } else if (c == quote) {
            if (i + 1 < literal.size() && literal[i + 1] == quote) {
                out += quote;
                ++i;
            } else {
                break;
            }
        } else {
            out += c;
        }
    }
    return out;
}

}
