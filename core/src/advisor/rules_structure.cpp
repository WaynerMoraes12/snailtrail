#include <algorithm>
#include <format>
#include <map>

#include "helpers.hpp"
#include "snailtrail/advisor/rules.hpp"
#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

using detail::join_words;

UnboundedWriteRule::UnboundedWriteRule()
    : Rule({"ST002", "unbounded-write", "UPDATE or DELETE without a WHERE clause", Severity::Critical}) {}

void UnboundedWriteRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const QueryFacts& f = *context.facts;
    const bool update = f.kind == sql::StatementKind::Update;
    if ((!update && f.kind != sql::StatementKind::Delete) || f.has_where || f.tables.empty()) return;

    const std::string& table = f.tables.front().name;
    std::string title;
    Severity severity = Severity::Critical;
    if (f.limit) {
        severity = Severity::Warning;
        title = std::format("{} without WHERE {} an arbitrary {} rows of {}", update ? "UPDATE" : "DELETE",
                            update ? "changes" : "removes", util::format_count(*f.limit), table);
    } else {
        title = std::format("{} without WHERE {} every row of {}", update ? "UPDATE" : "DELETE",
                            update ? "rewrites" : "removes", table);
    }
    std::string detail = std::format(
        "Every execution {} the whole table in one transaction: it locks every row, grows the undo "
        "log and replicates as one huge binlog event.",
        update ? "rewrites" : "empties");
    if (context.stats != nullptr) {
        detail += std::format(" It ran {} time(s), {} on average.", util::format_count(context.stats->calls()),
                              util::format_duration(context.stats->query_time().mean() / 1e6));
    }
    out.push_back(finding(
        severity, std::move(title), std::move(detail),
        std::format("Add a WHERE clause. For a deliberate bulk change, work in batches by primary key "
                    "(... WHERE id BETWEEN ? AND ?), committing between batches.")));
}

CartesianJoinRule::CartesianJoinRule()
    : Rule({"ST003", "cartesian-join", "Tables with no join condition between them", Severity::Critical}) {}

void CartesianJoinRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const QueryFacts& f = *context.facts;
    if (f.tables.size() < 2 || f.unresolved_join_column) return;

    std::map<std::string, std::string> parent;
    for (const auto& t : f.tables) parent[util::to_lower(t.ref())] = util::to_lower(t.ref());
    auto find = [&](std::string x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (const auto& link : f.links) {
        const std::string a = util::to_lower(link.left_ref);
        const std::string b = util::to_lower(link.right_ref);
        if (!parent.contains(a) || !parent.contains(b)) continue;
        parent[find(a)] = find(b);
    }

    std::vector<const TableUse*> representatives;
    std::vector<std::string> roots;
    for (const auto& t : f.tables) {
        const std::string root = find(util::to_lower(t.ref()));
        if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
            roots.push_back(root);
            representatives.push_back(&t);
        }
    }
    if (representatives.size() < 2) return;

    std::vector<std::string> names;
    for (const TableUse* t : representatives) names.push_back(t->derived ? t->ref() : t->name);
    std::string names_x;
    for (std::size_t i = 0; i < names.size(); ++i) names_x += (i > 0 ? " × " : "") + names[i];

    std::string detail = std::format(
        "Nothing in the ON or WHERE clauses relates {}, so every row of one is paired with every "
        "row of the other: the row count multiplies before any filter or aggregate applies.",
        join_words(names));
    if (context.stats != nullptr) {
        detail += std::format(" In the slow log it examined {} rows per execution.",
                              util::format_compact(context.stats->rows_examined().mean()));
    }

    std::string suggestion = "Add the missing join condition in ON or WHERE.";
    const TableUse* a = representatives[0];
    const TableUse* b = representatives[1];
    if (context.catalog != nullptr && !a->derived && !b->derived) {
        const schema::Table* ta = context.catalog->find_table(a->name);
        const schema::Table* tb = context.catalog->find_table(b->name);
        if (ta != nullptr && tb != nullptr) {
            const std::string a_key = detail::singular(a->name) + "_id";
            const std::string b_key = detail::singular(b->name) + "_id";
            if (tb->find_column(a_key) != nullptr && ta->find_column("id") != nullptr) {
                suggestion = std::format("Add the missing join condition, probably: {}.{} = {}.id",
                                         b->ref(), a_key, a->ref());
            } else if (ta->find_column(b_key) != nullptr && tb->find_column("id") != nullptr) {
                suggestion = std::format("Add the missing join condition, probably: {}.{} = {}.id",
                                         a->ref(), b_key, b->ref());
            }
        }
    }
    out.push_back(finding(Severity::Critical, names_x + ": no join condition (cartesian product)",
                          std::move(detail), std::move(suggestion)));
}

NotInSubqueryRule::NotInSubqueryRule()
    : Rule({"ST007", "not-in-subquery", "NOT IN (SELECT ...), which matches nothing when the subquery yields a NULL",
            Severity::Warning}) {}

namespace {

class NotInFinder final : public sql::RecursiveExprVisitor {
public:
    using RecursiveExprVisitor::visit;
    std::vector<const sql::InExpr*> found;

    void visit(const sql::UnaryExpr& e) override {
        if (e.op() == sql::UnaryOp::Not) negated_ = !negated_;
        RecursiveExprVisitor::visit(e);
        if (e.op() == sql::UnaryOp::Not) negated_ = !negated_;
    }

    void visit(const sql::InExpr& e) override {
        if (e.subquery() != nullptr && e.negated() != negated_) found.push_back(&e);
        RecursiveExprVisitor::visit(e);
    }

private:
    bool negated_ = false;
};

}

void NotInSubqueryRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    if (!context.facts->not_in_subquery) return;
    const sql::Expr* where = detail::where_of(*context.statement);
    if (where == nullptr) return;
    NotInFinder finder;
    where->accept(finder);

    for (const sql::InExpr* in : finder.found) {
        std::string outer = sql::to_sql(in->operand());
        const sql::SelectStatement& sub = *in->subquery();
        std::string rewrite = "NOT EXISTS (SELECT 1 FROM <table> WHERE <table>.<column> = " + outer + ")";
        std::string inner_column = "the subquery's column";
        if (!sub.from().empty() && !sub.items().empty() && sub.items().front().expr) {
            const sql::TableRef& table = sub.from().front();
            const std::string ref = table.reference_name();
            inner_column = sql::to_sql(*sub.items().front().expr);
            const std::string qualified_inner =
                inner_column.find('.') == std::string::npos ? ref + "." + inner_column : inner_column;
            if (outer.find('.') == std::string::npos && context.facts->tables.size() == 1) {
                outer = context.facts->tables.front().ref() + "." + outer;
            }
            rewrite = std::format("NOT EXISTS (SELECT 1 FROM {} WHERE {} = {})", sql::to_sql(table),
                                  qualified_inner, outer);
        }
        out.push_back(finding(
            Severity::Warning, "NOT IN (SELECT ...) matches nothing if the subquery returns a NULL",
            std::format("If {} is ever NULL, `{} NOT IN (...)` is never true and the query silently "
                        "returns no rows. MySQL has to guard against that case, which limits the "
                        "anti-join strategies it can choose.",
                        inner_column, sql::to_sql(in->operand())),
            "Use NOT EXISTS, which is NULL-safe and runs as an anti-join:\n" + rewrite));
    }
}

DeepPaginationRule::DeepPaginationRule()
    : Rule({"ST008", "deep-pagination", "LIMIT with a large OFFSET", Severity::Warning}) {}

void DeepPaginationRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const QueryFacts& f = *context.facts;
    if (!f.offset || *f.offset < 1000) return;
    const std::uint64_t offset = *f.offset;
    const std::uint64_t count = f.limit.value_or(0);

    std::string suggestion =
        "Use keyset pagination: remember the last row of the previous page and continue after it";
    if (!f.order_by.empty()) {
        const ColumnUse& key = f.order_by.front();
        std::vector<std::string> order;
        for (const auto& c : f.order_by) order.push_back(c.column + (f.order_by_descending ? " DESC" : ""));
        suggestion += std::format(":\n... WHERE {} {} ? ORDER BY {} LIMIT {}\n(add a unique tie-breaker such as "
                                  "the primary key if {} can repeat)",
                                  key.column, f.order_by_descending ? "<" : ">", util::join(order, ", "),
                                  count, key.column);
    } else {
        suggestion += ", ordered by an indexed unique key.";
    }
    out.push_back(finding(
        Severity::Warning,
        std::format("LIMIT {}, {} reads and discards {} rows", offset, count, util::format_count(offset)),
        std::format("MySQL cannot jump to an offset: it produces the first {} rows of the ordered "
                    "result and throws {} of them away. The cost grows with the page number, so the "
                    "last pages are the slowest.",
                    util::format_count(offset + count), util::format_count(offset)),
        std::move(suggestion)));
}

OrderByRandRule::OrderByRandRule()
    : Rule({"ST009", "order-by-rand", "ORDER BY RAND() to pick random rows", Severity::Warning}) {}

void OrderByRandRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const QueryFacts& f = *context.facts;
    if (!f.order_by_rand) return;
    const std::string table = f.tables.empty() ? "t" : f.tables.front().name;
    const std::string n = f.limit ? std::to_string(*f.limit) : "n";
    out.push_back(finding(
        Severity::Warning,
        std::format("ORDER BY RAND() sorts every row to return {}", f.limit ? n : "a few"),
        "RAND() is computed for every row that matches, the rows go to a temporary table and the "
        "whole set is sorted: the cost grows with the table, not with the LIMIT.",
        std::format("Pick random keys instead of sorting everything:\nSELECT ... FROM {0} WHERE id >= "
                    "FLOOR(RAND() * (SELECT MAX(id) FROM {0})) ORDER BY id LIMIT {1}\nor choose the ids "
                    "in the application from a cached id range.",
                    table, n)));
}

LargeInListRule::LargeInListRule()
    : Rule({"ST011", "large-in-list", "IN lists longer than eq_range_index_dive_limit", Severity::Info}) {}

void LargeInListRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const std::size_t n = context.facts->max_in_list;
    if (n <= 200) return;
    out.push_back(finding(
        Severity::Info, std::format("IN list with {} values", util::format_count(n)),
        "Above eq_range_index_dive_limit (200 by default) the optimizer stops diving into the index "
        "for each value and estimates from index statistics, so row estimates get coarser; long "
        "lists also cost parsing and optimization time on every call. The fingerprint folds every "
        "list length into this one class.",
        "Send the values in batches of up to 200, or load them into a temporary table and JOIN it."));
}

HavingWithoutAggregateRule::HavingWithoutAggregateRule()
    : Rule({"ST012", "having-without-aggregate", "HAVING conditions that belong in WHERE", Severity::Info}) {}

void HavingWithoutAggregateRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    if (!context.facts->having_without_aggregate) return;
    const sql::SelectStatement* select = detail::select_of(*context.statement);
    if (select == nullptr || select->having() == nullptr) return;
    const std::string condition = sql::to_sql(*select->having());
    out.push_back(finding(
        Severity::Info, std::format("HAVING {} filters on a column, not on an aggregate", condition),
        "HAVING is applied after grouping: every row is grouped first and whole groups are thrown "
        "away afterwards.",
        std::format("Move the condition to WHERE so rows are discarded before grouping:\n... WHERE {} "
                    "GROUP BY ...",
                    condition)));
}

SelectStarRule::SelectStarRule()
    : Rule({"ST013", "select-star", "SELECT * in the outer query", Severity::Info}) {}

void SelectStarRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const QueryFacts& f = *context.facts;
    if (f.kind != sql::StatementKind::Select || !f.select_star) return;

    std::vector<std::string> tables;
    const bool all = std::any_of(f.star_tables.begin(), f.star_tables.end(),
                                 [](const std::string& q) { return q.empty(); });
    for (const auto& t : f.tables) {
        if (t.derived) continue;
        const bool listed = std::any_of(f.star_tables.begin(), f.star_tables.end(),
                                        [&](const std::string& q) { return util::iequals(q, t.ref()); });
        if (all || listed) tables.push_back(t.name);
    }
    if (tables.empty()) return;

    std::string detail =
        "It returns columns the caller may not need (TEXT and BLOB columns cost extra page reads) "
        "and rules out covering indexes, where MySQL answers from the index alone.";
    if (context.catalog != nullptr) {
        for (const auto& name : tables) {
            const schema::Table* t = context.catalog->find_table(name);
            if (t == nullptr) continue;
            std::vector<std::string> wide;
            for (const auto& c : t->columns()) {
                if (util::istarts_with(c.type, "text") || util::istarts_with(c.type, "mediumtext") ||
                    util::istarts_with(c.type, "longtext") || util::istarts_with(c.type, "blob") ||
                    util::istarts_with(c.type, "json")) {
                    wide.push_back(c.name + " (" + c.type + ")");
                }
            }
            detail += std::format(" {} has {} columns{}.", name, t->columns().size(),
                                  wide.empty() ? "" : ", including " + join_words(wide));
        }
    }
    out.push_back(finding(Severity::Info, std::format("SELECT * reads every column of {}", join_words(tables)),
                          std::move(detail), "List only the columns the caller uses."));
}

}
