#include "snailtrail/advisor/query_facts.hpp"

#include <algorithm>

#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

std::string ColumnUse::display() const {
    if (!table.empty()) return table + "." + column;
    if (!ref.empty()) return ref + "." + column;
    return column;
}

bool ColumnUse::same_as(const ColumnUse& other) const noexcept {
    return util::iequals(ref, other.ref) && util::iequals(column, other.column);
}

bool TableUse::in_system_schema() const noexcept {
    return util::iequals(schema, "information_schema") || util::iequals(schema, "performance_schema") ||
           util::iequals(schema, "mysql") || util::iequals(schema, "sys");
}

const TableUse* QueryFacts::find_table(std::string_view ref) const noexcept {
    for (const TableUse& t : tables) {
        if (util::iequals(t.ref(), ref)) return &t;
    }
    return nullptr;
}

std::vector<const TableUse*> QueryFacts::base_tables() const {
    std::vector<const TableUse*> out;
    for (const TableUse& t : tables) {
        if (!t.derived) out.push_back(&t);
    }
    return out;
}

namespace {

template <typename T>
const T* node_as(const sql::Expr& e) {
    return dynamic_cast<const T*>(&e);
}

class ColumnCollector final : public sql::RecursiveExprVisitor {
public:
    using RecursiveExprVisitor::visit;
    std::vector<const sql::ColumnRef*> columns;
    void visit(const sql::ColumnRef& e) override { columns.push_back(&e); }
};

std::vector<const sql::ColumnRef*> columns_in(const sql::Expr& e) {
    ColumnCollector c;
    e.accept(c);
    return std::move(c.columns);
}

class AggregateFinder final : public sql::RecursiveExprVisitor {
public:
    using RecursiveExprVisitor::visit;
    bool found = false;
    void visit(const sql::FunctionCall& e) override {
        if (e.is_aggregate()) found = true;
        RecursiveExprVisitor::visit(e);
    }
};

bool contains_aggregate(const sql::Expr& e) {
    AggregateFinder f;
    e.accept(f);
    return f.found;
}

std::string wrapper_name(const sql::Expr& e) {
    if (const auto* f = node_as<sql::FunctionCall>(e)) return f->name();
    if (const auto* b = node_as<sql::BinaryExpr>(e)) {
        if (b->op() == sql::BinaryOp::JsonExtract || b->op() == sql::BinaryOp::JsonUnquoteExtract) {
            return "json";
        }
        return "arithmetic";
    }
    if (node_as<sql::CastExpr>(e) != nullptr) return "cast";
    if (const auto* u = node_as<sql::UnaryExpr>(e)) {
        return u->op() == sql::UnaryOp::Binary ? "binary" : "arithmetic";
    }
    if (node_as<sql::CaseExpr>(e) != nullptr) return "case";
    return "expression";
}

ValueKind value_kind(const sql::Expr& e) {
    if (const auto* lit = node_as<sql::Literal>(e)) {
        switch (lit->kind()) {
        case sql::LiteralKind::Number:
        case sql::LiteralKind::Hex: return ValueKind::Number;
        case sql::LiteralKind::String: return ValueKind::String;
        default: return ValueKind::Constant;
        }
    }
    if (node_as<sql::SubqueryExpr>(e) != nullptr) return ValueKind::Subquery;
    if (!columns_in(e).empty()) return ValueKind::Column;
    return ValueKind::Constant;
}

void flatten_and(const sql::Expr& e, std::vector<const sql::Expr*>& out) {
    if (const auto* b = node_as<sql::BinaryExpr>(e); b != nullptr && b->op() == sql::BinaryOp::And) {
        flatten_and(b->left(), out);
        flatten_and(b->right(), out);
    } else {
        out.push_back(&e);
    }
}

std::optional<std::uint64_t> literal_count(const sql::Expr* e) {
    if (e == nullptr) return std::nullopt;
    const auto* lit = node_as<sql::Literal>(*e);
    if (lit == nullptr || lit->kind() != sql::LiteralKind::Number) return std::nullopt;
    return util::parse_uint(lit->text());
}

class FactsCollector final : public sql::StatementVisitor {
public:
    FactsCollector(QueryFacts& facts, const schema::SchemaCatalog* catalog)
        : facts_(facts), catalog_(catalog) {}

    void visit(const sql::SelectStatement& s) override {
        facts_.kind = sql::StatementKind::Select;
        add_tables(s.from(), s.joins());
        for (const auto& item : s.items()) {
            if (item.star) {
                facts_.select_star = true;
                facts_.star_tables.push_back(item.star_qualifier);
            } else if (contains_aggregate(*item.expr)) {
                facts_.has_aggregate = true;
            }
        }
        add_where(s.where());
        add_join_conditions(s.joins());
        for (const auto& g : s.group_by()) {
            if (const auto* c = node_as<sql::ColumnRef>(*g)) facts_.group_by.push_back(resolve(*c));
        }
        if (s.having() != nullptr) {
            if (contains_aggregate(*s.having())) {
                facts_.has_aggregate = true;
            } else {
                facts_.having_without_aggregate = true;
            }
        }
        add_order_by(s.order_by());
        add_limit(s.limit());
    }

    void visit(const sql::UpdateStatement& s) override {
        facts_.kind = sql::StatementKind::Update;
        add_tables(s.tables(), s.joins());
        add_where(s.where());
        add_join_conditions(s.joins());
        add_order_by(s.order_by());
        add_limit(s.limit());
    }

    void visit(const sql::DeleteStatement& s) override {
        facts_.kind = sql::StatementKind::Delete;
        add_tables(s.from(), s.joins());
        add_where(s.where());
        add_join_conditions(s.joins());
        add_order_by(s.order_by());
        add_limit(s.limit());
    }

    void visit(const sql::InsertStatement& s) override {
        facts_.kind = s.kind();
        facts_.tables.push_back({s.table().name, {}, false, s.table().schema});
    }

    void visit(const sql::CreateTableStatement& s) override { facts_.kind = s.kind(); }
    void visit(const sql::AlterTableStatement& s) override { facts_.kind = s.kind(); }
    void visit(const sql::OtherStatement& s) override { facts_.kind = s.kind(); }

private:
    void add_tables(const std::vector<sql::TableRef>& from, const std::vector<sql::Join>& joins) {
        for (const auto& t : from) facts_.tables.push_back({t.name, t.alias, t.is_derived(), t.schema});
        for (const auto& j : joins) {
            facts_.tables.push_back({j.table.name, j.table.alias, j.table.is_derived(), j.table.schema});
        }
    }

    ColumnUse resolve(const sql::ColumnRef& c) const {
        ColumnUse use;
        use.column = c.name();
        if (!c.table().empty()) {
            if (const TableUse* t = facts_.find_table(c.table())) {
                use.ref = t->ref();
                if (!t->derived) use.table = t->name;
            } else {
                use.ref = c.table();
                use.table = c.table();
            }
            return use;
        }
        if (facts_.tables.size() == 1) {
            use.ref = facts_.tables[0].ref();
            if (!facts_.tables[0].derived) use.table = facts_.tables[0].name;
            return use;
        }
        if (catalog_ != nullptr) {
            const TableUse* match = nullptr;
            int matches = 0;
            for (const TableUse* t : facts_.base_tables()) {
                const schema::Table* table = catalog_->find_table(t->name);
                if (table != nullptr && table->find_column(c.name()) != nullptr) {
                    match = t;
                    ++matches;
                }
            }
            if (matches == 1) {
                use.table = match->name;
                use.ref = match->ref();
            }
        }
        return use;
    }

    void add_where(const sql::Expr* where) {
        if (where == nullptr) return;
        facts_.has_where = true;
        add_condition(*where, false);
    }

    void add_join_conditions(const std::vector<sql::Join>& joins) {
        for (std::size_t i = 0; i < joins.size(); ++i) {
            const sql::Join& j = joins[i];
            const std::string right = j.table.alias.empty() ? j.table.name : j.table.alias;
            if (j.condition) {
                add_condition(*j.condition, true);
            } else if (!j.using_columns.empty() || j.natural || j.kind == sql::JoinKind::Cross) {
                if (j.kind == sql::JoinKind::Cross && !j.natural && j.using_columns.empty()) {
                    facts_.explicit_cross_join = true;
                }
                const std::size_t index = facts_.tables.size() - joins.size() + i;
                if (index > 0) facts_.links.push_back({facts_.tables[index - 1].ref(), right});
            }
        }
    }

    void add_condition(const sql::Expr& condition, bool from_join) {
        std::vector<const sql::Expr*> conjuncts;
        flatten_and(condition, conjuncts);
        for (const sql::Expr* c : conjuncts) analyze(*c, false, false, from_join);
    }

    void analyze(const sql::Expr& e, bool under_or, bool negated, bool from_join) {
        if (const auto* b = node_as<sql::BinaryExpr>(e)) {
            if (b->op() == sql::BinaryOp::And) {
                analyze(b->left(), under_or, negated, from_join);
                analyze(b->right(), under_or, negated, from_join);
            } else if (b->op() == sql::BinaryOp::Or || b->op() == sql::BinaryOp::Xor) {
                if (!under_or) record_or_group(*b);
                analyze(b->left(), true, negated, from_join);
                analyze(b->right(), true, negated, from_join);
            } else if (sql::is_comparison(b->op())) {
                comparison(*b, under_or, negated, from_join);
            }
        } else if (const auto* u = node_as<sql::UnaryExpr>(e)) {
            if (u->op() == sql::UnaryOp::Not) analyze(u->operand(), under_or, !negated, from_join);
        } else if (const auto* in = node_as<sql::InExpr>(e)) {
            in_predicate(*in, under_or, negated, from_join);
        } else if (const auto* between = node_as<sql::BetweenExpr>(e)) {
            Predicate p = column_side(between->operand(), under_or, from_join);
            if (p.column.column.empty()) return;
            p.kind = PredicateKind::Range;
            p.negated = negated != between->negated();
            p.value = value_kind(between->low());
            p.value_sql = sql::to_sql(between->low());
            facts_.predicates.push_back(std::move(p));
        } else if (const auto* is = node_as<sql::IsExpr>(e)) {
            if (is->test() != sql::IsTest::Null) return;
            Predicate p = column_side(is->operand(), under_or, from_join);
            if (p.column.column.empty()) return;
            p.kind = (negated != is->negated()) ? PredicateKind::Range : PredicateKind::IsNull;
            p.value = ValueKind::Constant;
            p.value_sql = "NULL";
            facts_.predicates.push_back(std::move(p));
        }
    }

    Predicate column_side(const sql::Expr& side, bool under_or, bool from_join) const {
        Predicate p;
        p.under_or = under_or;
        p.from_join = from_join;
        const auto columns = columns_in(side);
        if (columns.empty()) return p;
        p.column = resolve(*columns.front());
        if (node_as<sql::ColumnRef>(side) == nullptr) {
            p.wrapper = wrapper_name(side);
            p.expression_sql = sql::to_sql(side);
        }
        return p;
    }

    void comparison(const sql::BinaryExpr& b, bool under_or, bool negated, bool from_join) {
        PredicateKind kind = PredicateKind::Other;
        bool flip = false;
        switch (b.op()) {
        case sql::BinaryOp::Eq:
        case sql::BinaryOp::NullSafeEq: kind = PredicateKind::Equality; break;
        case sql::BinaryOp::NotEq: kind = PredicateKind::Inequality; break;
        case sql::BinaryOp::Less:
        case sql::BinaryOp::LessEq:
        case sql::BinaryOp::Greater:
        case sql::BinaryOp::GreaterEq: kind = PredicateKind::Range; break;
        case sql::BinaryOp::Like: kind = PredicateKind::Like; break;
        case sql::BinaryOp::NotLike:
            kind = PredicateKind::Like;
            flip = true;
            break;
        default: kind = PredicateKind::Other;
        }

        const bool left_has = !columns_in(b.left()).empty();
        const bool right_has = !columns_in(b.right()).empty();
        const bool left_subquery = node_as<sql::SubqueryExpr>(b.left()) != nullptr;
        const bool right_subquery = node_as<sql::SubqueryExpr>(b.right()) != nullptr;

        if (left_has && right_has && !left_subquery && !right_subquery) {
            Predicate p = column_side(b.left(), under_or, from_join);
            const Predicate other = column_side(b.right(), under_or, from_join);
            p.kind = kind;
            p.negated = negated != flip;
            p.other = other.column;
            p.value = ValueKind::Column;
            p.value_sql = sql::to_sql(b.right());
            if (p.wrapper.empty() && !other.wrapper.empty()) p.wrapper = other.wrapper;
            if (p.column.ref.empty() || p.other->ref.empty()) {
                facts_.unresolved_join_column = true;
            } else if (!util::iequals(p.column.ref, p.other->ref)) {
                facts_.links.push_back({p.column.ref, p.other->ref});
            }
            facts_.predicates.push_back(std::move(p));
            return;
        }

        const sql::Expr* column_expr = nullptr;
        const sql::Expr* value_expr = nullptr;
        if (left_has && !left_subquery) {
            column_expr = &b.left();
            value_expr = &b.right();
        } else if (right_has && !right_subquery) {
            column_expr = &b.right();
            value_expr = &b.left();
        } else if (!left_has && !right_has) {
            return;
        } else {
            column_expr = left_subquery ? &b.right() : &b.left();
            value_expr = left_subquery ? &b.left() : &b.right();
        }

        Predicate p = column_side(*column_expr, under_or, from_join);
        if (p.column.column.empty()) return;
        p.kind = kind;
        p.negated = negated != flip;
        p.value = value_kind(*value_expr);
        p.value_sql = sql::to_sql(*value_expr);
        if (kind == PredicateKind::Like) {
            if (const auto* lit = node_as<sql::Literal>(*value_expr);
                lit != nullptr && lit->kind() == sql::LiteralKind::String) {
                p.like_pattern = lit->value();
            }
        }
        facts_.predicates.push_back(std::move(p));
    }

    void in_predicate(const sql::InExpr& in, bool under_or, bool negated, bool from_join) {
        Predicate p = column_side(in.operand(), under_or, from_join);
        if (p.column.column.empty()) return;
        p.kind = PredicateKind::In;
        p.negated = negated != in.negated();
        if (in.subquery() != nullptr) {
            p.value = ValueKind::Subquery;
            if (p.negated) facts_.not_in_subquery = true;
        } else {
            p.in_list_size = in.values().size();
            facts_.max_in_list = std::max(facts_.max_in_list, p.in_list_size);
            if (!in.values().empty()) {
                p.value = value_kind(*in.values().front());
                p.value_sql = sql::to_sql(*in.values().front());
            }
        }
        facts_.predicates.push_back(std::move(p));
    }

    void record_or_group(const sql::BinaryExpr& b) {
        OrGroup group;
        group.sql = sql::to_sql(b);
        for (const sql::ColumnRef* c : columns_in(b)) {
            ColumnUse use = resolve(*c);
            const bool seen = std::any_of(group.columns.begin(), group.columns.end(),
                                          [&](const ColumnUse& u) { return u.same_as(use); });
            if (!seen) group.columns.push_back(std::move(use));
        }
        facts_.or_groups.push_back(std::move(group));
    }

    void add_order_by(const std::vector<sql::OrderItem>& items) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const sql::OrderItem& item = items[i];
            if (i == 0) facts_.order_by_descending = item.descending;
            if (item.descending != items.front().descending) facts_.order_by_mixed_directions = true;
            if (const auto* c = node_as<sql::ColumnRef>(*item.expr)) {
                facts_.order_by.push_back(resolve(*c));
            } else if (const auto* f = node_as<sql::FunctionCall>(*item.expr);
                       f != nullptr && f->name() == "rand") {
                facts_.order_by_rand = true;
            } else {
                facts_.order_by_expression = true;
            }
        }
    }

    void add_limit(const std::optional<sql::Limit>& limit) {
        if (!limit) return;
        facts_.limit = literal_count(limit->count.get());
        facts_.offset = literal_count(limit->offset.get());
    }

    QueryFacts& facts_;
    const schema::SchemaCatalog* catalog_;
};

}

QueryFacts collect_facts(const sql::Statement& statement, const schema::SchemaCatalog* catalog) {
    QueryFacts facts;
    FactsCollector collector(facts, catalog);
    statement.accept(collector);
    return facts;
}

}
