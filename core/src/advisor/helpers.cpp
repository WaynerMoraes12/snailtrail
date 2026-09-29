#include "helpers.hpp"

#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor::detail {

namespace {

class WhereOf final : public sql::StatementVisitor {
public:
    const sql::Expr* where = nullptr;
    const sql::SelectStatement* select = nullptr;

    void visit(const sql::SelectStatement& s) override {
        where = s.where();
        select = &s;
    }
    void visit(const sql::UpdateStatement& s) override { where = s.where(); }
    void visit(const sql::DeleteStatement& s) override { where = s.where(); }
    void visit(const sql::InsertStatement&) override {}
    void visit(const sql::CreateTableStatement&) override {}
    void visit(const sql::AlterTableStatement&) override {}
    void visit(const sql::OtherStatement&) override {}
};

}

const sql::Expr* where_of(const sql::Statement& statement) {
    WhereOf w;
    statement.accept(w);
    return w.where;
}

const sql::SelectStatement* select_of(const sql::Statement& statement) {
    WhereOf w;
    statement.accept(w);
    return w.select;
}

std::string join_words(const std::vector<std::string>& words) {
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) out += (i + 1 == words.size()) ? " and " : ", ";
        out += words[i];
    }
    return out;
}

std::string singular(std::string_view name) {
    std::string s = util::to_lower(name);
    if (s.ends_with("ies") && s.size() > 3) return s.substr(0, s.size() - 3) + "y";
    if (s.ends_with("ses") || s.ends_with("xes")) return s.substr(0, s.size() - 2);
    if (s.ends_with('s') && !s.ends_with("ss")) return s.substr(0, s.size() - 1);
    return s;
}

std::string strip_quotes(std::string_view literal) {
    if (literal.size() >= 2 && (literal.front() == '\'' || literal.front() == '"') &&
        literal.back() == literal.front()) {
        return std::string(literal.substr(1, literal.size() - 2));
    }
    return std::string(literal);
}

const schema::Column* find_column(const schema::SchemaCatalog* catalog, const ColumnUse& use) {
    if (catalog == nullptr || !use.resolved()) return nullptr;
    const schema::Table* t = catalog->find_table(use.table);
    return t == nullptr ? nullptr : t->find_column(use.column);
}

bool leads_an_index(const schema::SchemaCatalog* catalog, const ColumnUse& use) {
    if (catalog == nullptr || !use.resolved()) return false;
    const schema::Table* t = catalog->find_table(use.table);
    if (t == nullptr) return false;
    for (const auto& index : t->indexes()) {
        if (index.is_btree() && !index.columns.empty() && util::iequals(index.columns.front(), use.column)) {
            return true;
        }
    }
    return false;
}

}
