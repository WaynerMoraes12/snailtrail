#include "snailtrail/sql/ast_printer.hpp"

#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

namespace {

AstNode node(std::string label, std::vector<AstNode> children = {}) {
    return AstNode{std::move(label), std::move(children)};
}

std::string_view literal_kind_name(LiteralKind kind) noexcept {
    switch (kind) {
    case LiteralKind::String: return "String";
    case LiteralKind::Number: return "Number";
    case LiteralKind::Hex: return "Hex";
    case LiteralKind::Null: return "Null";
    case LiteralKind::Boolean: return "Boolean";
    case LiteralKind::Placeholder: return "Placeholder";
    case LiteralKind::Variable: return "Variable";
    case LiteralKind::Keyword: return "Keyword";
    }
    return "Literal";
}

AstNode describe_select(const SelectStatement& s);

class ExprDescriber final : public ExprVisitor {
public:
    AstNode result;

    static AstNode of(const Expr& e) {
        ExprDescriber d;
        e.accept(d);
        return std::move(d.result);
    }

    void visit(const Literal& e) override {
        result = node(std::string(literal_kind_name(e.kind())) + " " + e.text());
    }

    void visit(const ColumnRef& e) override { result = node("Column " + e.qualified_name()); }

    void visit(const UnaryExpr& e) override {
        static constexpr const char* names[] = {"NOT", "Negate", "Plus", "BitNot", "BINARY"};
        result = node(names[static_cast<int>(e.op())], {of(e.operand())});
    }

    void visit(const BinaryExpr& e) override {
        result = node(std::string(binary_op_symbol(e.op())), {of(e.left()), of(e.right())});
    }

    void visit(const InExpr& e) override {
        AstNode n = node(e.negated() ? "NOT IN" : "IN", {of(e.operand())});
        if (e.subquery() != nullptr) {
            n.children.push_back(node("subquery", {describe_select(*e.subquery())}));
        } else {
            AstNode list = node("list (" + std::to_string(e.values().size()) + ")");
            for (const auto& v : e.values()) list.children.push_back(of(*v));
            n.children.push_back(std::move(list));
        }
        result = std::move(n);
    }

    void visit(const BetweenExpr& e) override {
        result = node(e.negated() ? "NOT BETWEEN" : "BETWEEN",
                      {of(e.operand()), of(e.low()), of(e.high())});
    }

    void visit(const IsExpr& e) override {
        static constexpr const char* tests[] = {"NULL", "TRUE", "FALSE", "UNKNOWN"};
        std::string label = e.negated() ? "IS NOT " : "IS ";
        label += tests[static_cast<int>(e.test())];
        result = node(std::move(label), {of(e.operand())});
    }

    void visit(const FunctionCall& e) override {
        std::string label = "Function " + util::to_upper(e.name());
        if (e.star()) label += "(*)";
        if (e.distinct()) label += " DISTINCT";
        if (e.is_aggregate()) label += " [aggregate]";
        if (e.windowed()) label += " [window]";
        AstNode n = node(std::move(label));
        for (const auto& a : e.args()) n.children.push_back(of(*a));
        result = std::move(n);
    }

    void visit(const CaseExpr& e) override {
        AstNode n = node("CASE");
        if (e.operand() != nullptr) n.children.push_back(of(*e.operand()));
        for (const auto& b : e.branches()) {
            n.children.push_back(node("WHEN", {of(*b.condition), of(*b.result)}));
        }
        if (e.else_result() != nullptr) n.children.push_back(node("ELSE", {of(*e.else_result())}));
        result = std::move(n);
    }

    void visit(const CastExpr& e) override {
        result = node("CAST AS " + util::to_upper(e.target_type()), {of(e.operand())});
    }

    void visit(const IntervalExpr& e) override {
        result = node("INTERVAL " + e.unit(), {of(e.value())});
    }

    void visit(const SubqueryExpr& e) override {
        static constexpr const char* names[] = {"Subquery", "EXISTS", "ANY", "ALL"};
        result = node(names[static_cast<int>(e.quantifier())], {describe_select(e.query())});
    }

    void visit(const RowExpr& e) override {
        AstNode n = node("Row");
        for (const auto& i : e.items()) n.children.push_back(ExprDescriber::of(*i));
        result = std::move(n);
    }
};

AstNode describe_table(const TableRef& t) {
    if (t.is_derived()) {
        return node("Derived AS " + t.alias, {describe_select(*t.derived)});
    }
    std::string label = "Table ";
    if (!t.schema.empty()) label += t.schema + ".";
    label += t.name;
    if (!t.alias.empty()) label += " AS " + t.alias;
    return node(std::move(label));
}

void add_tables(AstNode& parent, const std::vector<TableRef>& from, const std::vector<Join>& joins) {
    if (!from.empty()) {
        AstNode n = node("FROM");
        for (const auto& t : from) n.children.push_back(describe_table(t));
        parent.children.push_back(std::move(n));
    }
    for (const Join& j : joins) {
        std::string label = j.natural ? "NATURAL " : "";
        label += join_kind_name(j.kind);
        AstNode n = node(std::move(label), {describe_table(j.table)});
        if (j.condition) n.children.push_back(node("ON", {ExprDescriber::of(*j.condition)}));
        if (!j.using_columns.empty()) {
            n.children.push_back(node("USING (" + util::join(j.using_columns, ", ") + ")"));
        }
        parent.children.push_back(std::move(n));
    }
}

void add_expr(AstNode& parent, std::string label, const Expr* e) {
    if (e != nullptr) parent.children.push_back(node(std::move(label), {ExprDescriber::of(*e)}));
}

void add_order(AstNode& parent, const std::vector<OrderItem>& items) {
    if (items.empty()) return;
    AstNode n = node("ORDER BY");
    for (const auto& i : items) {
        AstNode item = ExprDescriber::of(*i.expr);
        if (i.descending) item.label += " DESC";
        n.children.push_back(std::move(item));
    }
    parent.children.push_back(std::move(n));
}

void add_limit(AstNode& parent, const std::optional<Limit>& limit) {
    if (!limit) return;
    AstNode n = node("LIMIT", {ExprDescriber::of(*limit->count)});
    if (limit->offset) n.children.push_back(node("OFFSET", {ExprDescriber::of(*limit->offset)}));
    parent.children.push_back(std::move(n));
}

void add_assignments(AstNode& parent, const std::vector<Assignment>& assignments,
                     std::string label) {
    if (assignments.empty()) return;
    AstNode n = node(std::move(label));
    for (const auto& a : assignments) {
        n.children.push_back(node("= " + a.column->qualified_name(), {ExprDescriber::of(*a.value)}));
    }
    parent.children.push_back(std::move(n));
}

AstNode describe_select(const SelectStatement& s) {
    AstNode root = node(s.distinct() ? "SELECT DISTINCT" : "SELECT");
    for (const auto& cte : s.ctes()) {
        root.children.push_back(node("WITH " + cte.name, {describe_select(*cte.query)}));
    }
    AstNode items = node("items");
    for (const auto& item : s.items()) {
        if (item.star) {
            items.children.push_back(
                node(item.star_qualifier.empty() ? "*" : item.star_qualifier + ".*"));
        } else {
            AstNode n = ExprDescriber::of(*item.expr);
            if (!item.alias.empty()) n.label += " AS " + item.alias;
            items.children.push_back(std::move(n));
        }
    }
    root.children.push_back(std::move(items));
    add_tables(root, s.from(), s.joins());
    add_expr(root, "WHERE", s.where());
    if (!s.group_by().empty()) {
        AstNode n = node(s.with_rollup() ? "GROUP BY WITH ROLLUP" : "GROUP BY");
        for (const auto& g : s.group_by()) n.children.push_back(ExprDescriber::of(*g));
        root.children.push_back(std::move(n));
    }
    add_expr(root, "HAVING", s.having());
    add_order(root, s.order_by());
    add_limit(root, s.limit());
    if (s.lock_mode() == LockMode::ForUpdate) root.children.push_back(node("FOR UPDATE"));
    if (s.lock_mode() == LockMode::ForShare) root.children.push_back(node("FOR SHARE"));
    for (const auto& b : s.set_operations()) {
        root.children.push_back(node(std::string(set_operator_name(b.op)), {describe_select(*b.select)}));
    }
    return root;
}

AstNode describe_index(const IndexDefinition& index) {
    std::string label = std::string(index_kind_name(index.kind));
    if (!index.name.empty() && index.kind != IndexKind::Primary) label += " " + index.name;
    label += " (" + util::join(index.columns, ", ") + ")";
    return node(std::move(label));
}

class StatementDescriber final : public StatementVisitor {
public:
    AstNode result;

    void visit(const SelectStatement& s) override { result = describe_select(s); }

    void visit(const InsertStatement& s) override {
        AstNode root = node(s.replace() ? "REPLACE" : "INSERT");
        root.children.push_back(describe_table(s.table()));
        if (!s.columns().empty()) {
            root.children.push_back(node("columns (" + util::join(s.columns(), ", ") + ")"));
        }
        if (s.row_count() > 0) {
            root.children.push_back(node("VALUES: " + std::to_string(s.row_count()) + " row(s)"));
        }
        if (s.select() != nullptr) root.children.push_back(describe_select(*s.select()));
        add_assignments(root, s.assignments(), "SET");
        add_assignments(root, s.on_duplicate_key_update(), "ON DUPLICATE KEY UPDATE");
        result = std::move(root);
    }

    void visit(const UpdateStatement& s) override {
        AstNode root = node("UPDATE");
        add_tables(root, s.tables(), s.joins());
        add_assignments(root, s.assignments(), "SET");
        add_expr(root, "WHERE", s.where());
        add_order(root, s.order_by());
        add_limit(root, s.limit());
        result = std::move(root);
    }

    void visit(const DeleteStatement& s) override {
        AstNode root = node("DELETE");
        if (!s.targets().empty()) {
            root.children.push_back(node("targets (" + util::join(s.targets(), ", ") + ")"));
        }
        add_tables(root, s.from(), s.joins());
        add_expr(root, "WHERE", s.where());
        add_order(root, s.order_by());
        add_limit(root, s.limit());
        result = std::move(root);
    }

    void visit(const CreateTableStatement& s) override {
        AstNode root = node(s.temporary() ? "CREATE TEMPORARY TABLE" : "CREATE TABLE");
        root.children.push_back(describe_table(s.table()));
        AstNode columns = node("columns");
        for (const auto& c : s.columns()) {
            std::string label = c.name + " " + c.type;
            if (!c.nullable) label += " NOT NULL";
            if (c.auto_increment) label += " AUTO_INCREMENT";
            columns.children.push_back(node(std::move(label)));
        }
        root.children.push_back(std::move(columns));
        AstNode indexes = node("indexes");
        for (const auto& i : s.indexes()) indexes.children.push_back(describe_index(i));
        if (!indexes.children.empty()) root.children.push_back(std::move(indexes));
        result = std::move(root);
    }

    void visit(const AlterTableStatement& s) override {
        AstNode root = node("ALTER TABLE");
        root.children.push_back(describe_table(s.table()));
        for (const auto& c : s.added_columns()) root.children.push_back(node("ADD COLUMN " + c.name));
        for (const auto& i : s.added_indexes()) {
            AstNode n = describe_index(i);
            n.label = "ADD " + n.label;
            root.children.push_back(std::move(n));
        }
        for (const auto& d : s.dropped_indexes()) root.children.push_back(node("DROP INDEX " + d));
        result = std::move(root);
    }

    void visit(const OtherStatement& s) override {
        result = node(std::string(statement_kind_name(s.kind())) + " (" + util::to_upper(s.keyword()) + ")");
    }
};

void render(const AstNode& n, const std::string& prefix, bool last, bool root, std::string& out) {
    if (root) {
        out += n.label + "\n";
    } else {
        out += prefix + (last ? "└── " : "├── ") + n.label + "\n";
    }
    const std::string child_prefix = root ? "" : prefix + (last ? "    " : "│   ");
    for (std::size_t i = 0; i < n.children.size(); ++i) {
        render(n.children[i], child_prefix, i + 1 == n.children.size(), false, out);
    }
}

}

AstNode describe(const Statement& statement) {
    StatementDescriber d;
    statement.accept(d);
    return std::move(d.result);
}

AstNode describe(const Expr& expr) { return ExprDescriber::of(expr); }

std::string render_tree(const AstNode& root) {
    std::string out;
    render(root, "", true, true, out);
    return out;
}

std::string dump_ast(const Statement& statement) { return render_tree(describe(statement)); }

std::string dump_ast(const Expr& expr) { return render_tree(describe(expr)); }

}
