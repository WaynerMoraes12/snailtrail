#include "snailtrail/sql/statement_kind.hpp"

#include <array>

#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

std::string_view statement_kind_name(StatementKind kind) noexcept {
    switch (kind) {
    case StatementKind::Select: return "SELECT";
    case StatementKind::Insert: return "INSERT";
    case StatementKind::Update: return "UPDATE";
    case StatementKind::Delete: return "DELETE";
    case StatementKind::Replace: return "REPLACE";
    case StatementKind::Call: return "CALL";
    case StatementKind::Ddl: return "DDL";
    case StatementKind::Transaction: return "TRANSACTION";
    case StatementKind::Utility: return "UTILITY";
    case StatementKind::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

namespace {

StatementKind kind_of_word(std::string_view w) noexcept {
    using util::iequals;
    if (iequals(w, "select") || iequals(w, "table") || iequals(w, "values")) {
        return StatementKind::Select;
    }
    if (iequals(w, "insert")) return StatementKind::Insert;
    if (iequals(w, "update")) return StatementKind::Update;
    if (iequals(w, "delete")) return StatementKind::Delete;
    if (iequals(w, "replace")) return StatementKind::Replace;
    if (iequals(w, "call")) return StatementKind::Call;

    static constexpr std::array<std::string_view, 5> ddl = {"create", "alter", "drop", "truncate",
                                                            "rename"};
    for (auto k : ddl) {
        if (iequals(w, k)) return StatementKind::Ddl;
    }
    static constexpr std::array<std::string_view, 7> tx = {
        "begin", "commit", "rollback", "start", "savepoint", "release", "xa"};
    for (auto k : tx) {
        if (iequals(w, k)) return StatementKind::Transaction;
    }
    static constexpr std::array<std::string_view, 25> utility = {
        "set",     "show",   "use",       "explain",  "describe", "desc",     "analyze",
        "optimize", "check", "checksum",  "repair",   "flush",    "kill",     "grant",
        "revoke",  "lock",   "unlock",    "load",     "handler",  "do",       "prepare",
        "execute", "deallocate", "reset", "purge"};
    for (auto k : utility) {
        if (iequals(w, k)) return StatementKind::Utility;
    }
    return StatementKind::Unknown;
}

}

StatementKind classify_statement(std::span<const Token> tokens) noexcept {
    std::size_t i = 0;
    while (i < tokens.size() && tokens[i].kind == TokenKind::LParen) ++i;
    if (i >= tokens.size() || tokens[i].kind != TokenKind::Word) return StatementKind::Unknown;

    if (!tokens[i].is_word("with")) return kind_of_word(tokens[i].text);

    int depth = 0;
    for (++i; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == TokenKind::LParen) {
            ++depth;
        } else if (t.kind == TokenKind::RParen) {
            --depth;
        } else if (depth == 0 && t.kind == TokenKind::Word) {
            const StatementKind k = kind_of_word(t.text);
            if (is_dml(k)) return k;
        }
    }
    return StatementKind::Unknown;
}

}
