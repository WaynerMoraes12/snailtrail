#include "snailtrail/sql/sql_writer.hpp"

#include "snailtrail/sql/keywords.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

namespace {

constexpr int atomic_precedence = 100;
constexpr int not_precedence = 5;
constexpr int predicate_precedence = 6;
constexpr int bit_or_precedence = 7;
constexpr int unary_precedence = 13;

class PrecedenceOf final : public ExprVisitor {
public:
    int value = atomic_precedence;

    void visit(const Literal&) override { value = atomic_precedence; }
    void visit(const ColumnRef&) override { value = atomic_precedence; }
    void visit(const UnaryExpr& e) override {
        value = e.op() == UnaryOp::Not ? not_precedence : unary_precedence;
    }
    void visit(const BinaryExpr& e) override { value = precedence(e.op()); }
    void visit(const InExpr&) override { value = predicate_precedence; }
    void visit(const BetweenExpr&) override { value = predicate_precedence; }
    void visit(const IsExpr&) override { value = predicate_precedence; }
    void visit(const FunctionCall&) override { value = atomic_precedence; }
    void visit(const CaseExpr&) override { value = atomic_precedence; }
    void visit(const CastExpr&) override { value = atomic_precedence; }
    void visit(const IntervalExpr&) override { value = atomic_precedence; }
    void visit(const SubqueryExpr&) override { value = atomic_precedence; }
    void visit(const RowExpr&) override { value = atomic_precedence; }
};

int precedence_of(const Expr& e) {
    PrecedenceOf p;
    e.accept(p);
    return p.value;
}

class SqlWriter final : public ExprVisitor {
public:
    std::string out;

    void write(const Expr& e) { e.accept(*this); }

    void write_child(const Expr& e, int minimum) {
        if (precedence_of(e) < minimum) {
            out += '(';
            write(e);
            out += ')';
        } else {
            write(e);
        }
    }

    void write_list(const std::vector<ExprPtr>& items) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) out += ", ";
            write(*items[i]);
        }
    }

    void visit(const Literal& e) override { out += e.text(); }

    void visit(const ColumnRef& e) override {
        if (!e.schema().empty()) out += quote_identifier(e.schema()) + ".";
        if (!e.table().empty()) out += quote_identifier(e.table()) + ".";
        out += quote_identifier(e.name());
    }

    void visit(const UnaryExpr& e) override {
        switch (e.op()) {
        case UnaryOp::Not: out += "NOT "; break;
        case UnaryOp::Negate: out += "-"; break;
        case UnaryOp::Plus: out += "+"; break;
        case UnaryOp::BitNot: out += "~"; break;
        case UnaryOp::Binary: out += "BINARY "; break;
        }
        write_child(e.operand(), e.op() == UnaryOp::Not ? not_precedence : unary_precedence);
    }

    void visit(const BinaryExpr& e) override {
        const int p = precedence(e.op());
        write_child(e.left(), p);
        const bool json = e.op() == BinaryOp::JsonExtract || e.op() == BinaryOp::JsonUnquoteExtract;
        if (json) {
            out += binary_op_symbol(e.op());
        } else {
            out += ' ';
            out += binary_op_symbol(e.op());
            out += ' ';
        }
        write_child(e.right(), p + 1);
    }

    void visit(const InExpr& e) override {
        write_child(e.operand(), bit_or_precedence);
        out += e.negated() ? " NOT IN (" : " IN (";
        if (e.subquery() != nullptr) {
            out += to_sql(*e.subquery());
        } else {
            write_list(e.values());
        }
        out += ')';
    }

    void visit(const BetweenExpr& e) override {
        write_child(e.operand(), bit_or_precedence);
        out += e.negated() ? " NOT BETWEEN " : " BETWEEN ";
        write_child(e.low(), bit_or_precedence);
        out += " AND ";
        write_child(e.high(), bit_or_precedence);
    }

    void visit(const IsExpr& e) override {
        write_child(e.operand(), bit_or_precedence);
        out += e.negated() ? " IS NOT " : " IS ";
        switch (e.test()) {
        case IsTest::Null: out += "NULL"; break;
        case IsTest::True: out += "TRUE"; break;
        case IsTest::False: out += "FALSE"; break;
        case IsTest::Unknown: out += "UNKNOWN"; break;
        }
    }

    void visit(const FunctionCall& e) override {
        const auto& args = e.args();
        if (e.name() == "match" && !args.empty()) {
            out += "MATCH(";
            for (std::size_t i = 0; i + 1 < args.size(); ++i) {
                if (i > 0) out += ", ";
                write(*args[i]);
            }
            out += ") AGAINST(";
            write(*args.back());
            out += ')';
            return;
        }
        if (e.name() == "extract" && args.size() == 2) {
            out += "EXTRACT(";
            write(*args[0]);
            out += " FROM ";
            write(*args[1]);
            out += ')';
            return;
        }
        if (e.name() == "position" && args.size() == 2) {
            out += "POSITION(";
            write_child(*args[0], bit_or_precedence);
            out += " IN ";
            write(*args[1]);
            out += ')';
            return;
        }
        out += util::to_upper(e.name());
        const bool niladic = args.empty() && !e.star() && e.name().starts_with("current_");
        if (niladic) return;
        out += '(';
        if (e.star()) {
            out += '*';
        } else {
            if (e.distinct()) out += "DISTINCT ";
            write_list(args);
        }
        out += ')';
        if (e.windowed()) out += " OVER ()";
    }

    void visit(const CaseExpr& e) override {
        out += "CASE";
        if (e.operand() != nullptr) {
            out += ' ';
            write(*e.operand());
        }
        for (const auto& b : e.branches()) {
            out += " WHEN ";
            write(*b.condition);
            out += " THEN ";
            write(*b.result);
        }
        if (e.else_result() != nullptr) {
            out += " ELSE ";
            write(*e.else_result());
        }
        out += " END";
    }

    void visit(const CastExpr& e) override {
        if (e.target_type().starts_with("using ")) {
            out += "CONVERT(";
            write(e.operand());
            out += " USING " + e.target_type().substr(6) + ")";
            return;
        }
        out += "CAST(";
        write(e.operand());
        out += " AS " + util::to_upper(e.target_type()) + ")";
    }

    void visit(const IntervalExpr& e) override {
        out += "INTERVAL ";
        write_child(e.value(), unary_precedence);
        out += ' ';
        out += e.unit();
    }

    void visit(const SubqueryExpr& e) override {
        switch (e.quantifier()) {
        case Quantifier::Exists: out += "EXISTS "; break;
        case Quantifier::Any: out += "ANY "; break;
        case Quantifier::All: out += "ALL "; break;
        case Quantifier::None: break;
        }
        out += '(' + to_sql(e.query()) + ')';
    }

    void visit(const RowExpr& e) override {
        out += '(';
        write_list(e.items());
        out += ')';
    }
};

void append_order_by(std::string& out, const std::vector<OrderItem>& items) {
    if (items.empty()) return;
    out += " ORDER BY ";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ", ";
        out += to_sql(*items[i].expr);
        if (items[i].descending) out += " DESC";
    }
}

void append_limit(std::string& out, const std::optional<Limit>& limit) {
    if (!limit) return;
    out += " LIMIT ";
    if (limit->offset) out += to_sql(*limit->offset) + ", ";
    out += to_sql(*limit->count);
}

std::string select_core(const SelectStatement& s) {
    std::string out = "SELECT ";
    if (s.distinct()) out += "DISTINCT ";
    for (std::size_t i = 0; i < s.items().size(); ++i) {
        const SelectItem& item = s.items()[i];
        if (i > 0) out += ", ";
        if (item.star) {
            out += item.star_qualifier.empty() ? "*" : item.star_qualifier + ".*";
        } else {
            out += to_sql(*item.expr);
            if (!item.alias.empty()) out += " AS " + quote_identifier(item.alias);
        }
    }
    if (!s.from().empty()) {
        out += " FROM ";
        for (std::size_t i = 0; i < s.from().size(); ++i) {
            if (i > 0) out += ", ";
            out += to_sql(s.from()[i]);
        }
    }
    for (const Join& j : s.joins()) {
        out += ' ';
        if (j.natural) out += "NATURAL ";
        out += join_kind_name(j.kind);
        out += ' ' + to_sql(j.table);
        if (j.condition) {
            out += " ON " + to_sql(*j.condition);
        } else if (!j.using_columns.empty()) {
            out += " USING (" + util::join(j.using_columns, ", ") + ")";
        }
    }
    if (s.where() != nullptr) out += " WHERE " + to_sql(*s.where());
    if (!s.group_by().empty()) {
        out += " GROUP BY ";
        for (std::size_t i = 0; i < s.group_by().size(); ++i) {
            if (i > 0) out += ", ";
            out += to_sql(*s.group_by()[i]);
        }
        if (s.with_rollup()) out += " WITH ROLLUP";
    }
    if (s.having() != nullptr) out += " HAVING " + to_sql(*s.having());
    return out;
}

}

std::string quote_identifier(std::string_view name) {
    bool plain = !name.empty() && !is_reserved_word(name);
    bool all_digits = true;
    for (char c : name) {
        const bool word = util::is_alpha(c) || util::is_digit(c) || c == '_' || c == '$' ||
                          static_cast<unsigned char>(c) >= 0x80;
        plain = plain && word;
        all_digits = all_digits && util::is_digit(c);
    }
    if (plain && !all_digits) return std::string(name);
    std::string out = "`";
    for (char c : name) {
        if (c == '`') out += '`';
        out += c;
    }
    return out + "`";
}

std::string to_sql(const Expr& expr) {
    SqlWriter writer;
    writer.write(expr);
    return std::move(writer.out);
}

std::string to_sql(const TableRef& table) {
    std::string out;
    if (table.is_derived()) {
        out = "(" + to_sql(*table.derived) + ")";
    } else {
        if (!table.schema.empty()) out += quote_identifier(table.schema) + ".";
        out += quote_identifier(table.name);
    }
    if (!table.alias.empty()) out += " AS " + quote_identifier(table.alias);
    return out;
}

std::string to_sql(const SelectStatement& select) {
    std::string out;
    if (!select.ctes().empty()) {
        out += "WITH ";
        for (std::size_t i = 0; i < select.ctes().size(); ++i) {
            const CommonTableExpr& cte = select.ctes()[i];
            if (i > 0) out += ", ";
            out += quote_identifier(cte.name);
            if (!cte.columns.empty()) out += " (" + util::join(cte.columns, ", ") + ")";
            out += " AS (" + to_sql(*cte.query) + ")";
        }
        out += ' ';
    }
    out += select_core(select);
    for (const SetBranch& branch : select.set_operations()) {
        out += ' ';
        out += set_operator_name(branch.op);
        out += ' ';
        out += to_sql(*branch.select);
    }
    append_order_by(out, select.order_by());
    append_limit(out, select.limit());
    if (select.lock_mode() == LockMode::ForUpdate) out += " FOR UPDATE";
    if (select.lock_mode() == LockMode::ForShare) out += " FOR SHARE";
    return out;
}

}
