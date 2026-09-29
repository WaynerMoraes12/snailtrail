#include "snailtrail/advisor/index_advisor.hpp"

#include <algorithm>
#include <format>

#include "helpers.hpp"
#include "snailtrail/advisor/rules.hpp"
#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

namespace {

constexpr std::size_t max_equality_columns = 4;
constexpr std::size_t max_tail_columns = 3;

bool contains(const std::vector<std::string>& columns, const std::string& c) {
    return std::any_of(columns.begin(), columns.end(),
                       [&](const std::string& x) { return util::iequals(x, c); });
}

void add_unique(std::vector<std::string>& columns, const std::string& c) {
    if (!contains(columns, c)) columns.push_back(c);
}

bool is_prefix_like(const Predicate& p) {
    return p.kind == PredicateKind::Like && !p.like_pattern.empty() &&
           p.like_pattern.front() != '%' && p.like_pattern.front() != '_';
}

std::vector<std::string> columns_on(const std::vector<ColumnUse>& uses, const TableUse& table) {
    std::vector<std::string> out;
    for (const ColumnUse& u : uses) {
        if (!util::iequals(u.ref, table.ref())) return {};
        add_unique(out, u.column);
    }
    return out;
}

}

using detail::join_words;

std::vector<std::string> IndexCandidate::columns() const {
    std::vector<std::string> out = equality;
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

bool usable_for_index(const Predicate& p, const schema::SchemaCatalog* catalog) {
    if (!p.sargable() || p.under_or) return false;
    if (catalog != nullptr && p.value == ValueKind::Number && p.column.resolved()) {
        if (const schema::Table* t = catalog->find_table(p.column.table)) {
            if (const schema::Column* c = t->find_column(p.column.column)) {
                if (schema::is_textual(c->category)) return false;
            }
        }
    }
    return true;
}

std::string index_name(const std::string& table, const std::vector<std::string>& columns) {
    std::string name = "idx_" + util::to_lower(table);
    for (const auto& c : columns) {
        name += "_";
        for (char ch : c) {
            name += (util::is_alpha(ch) || util::is_digit(ch) || ch == '_') ? util::to_lower(ch) : '_';
        }
    }
    if (name.size() > 64) name.resize(64);
    return name;
}

std::vector<IndexCandidate> suggest_indexes(const QueryFacts& facts,
                                            const schema::SchemaCatalog* catalog) {
    std::vector<IndexCandidate> out;
    if (facts.kind != sql::StatementKind::Select && facts.kind != sql::StatementKind::Update &&
        facts.kind != sql::StatementKind::Delete) {
        return out;
    }

    for (const TableUse* table : facts.base_tables()) {
        std::vector<std::string> equality;
        std::vector<std::string> join_equality;
        std::string range;
        for (const Predicate& p : facts.predicates) {
            if (p.is_join()) {
                if (p.kind != PredicateKind::Equality || !p.sargable() || p.under_or) continue;
                if (util::iequals(p.column.ref, table->ref())) add_unique(join_equality, p.column.column);
                if (util::iequals(p.other->ref, table->ref())) add_unique(join_equality, p.other->column);
                continue;
            }
            if (!util::iequals(p.column.ref, table->ref()) || !usable_for_index(p, catalog)) continue;
            if (p.kind == PredicateKind::Equality || p.kind == PredicateKind::IsNull ||
                p.kind == PredicateKind::In) {
                add_unique(equality, p.column.column);
            } else if ((p.kind == PredicateKind::Range || is_prefix_like(p)) && range.empty()) {
                range = p.column.column;
            }
        }
        if (contains(equality, range)) range.clear();

        IndexCandidate candidate;
        candidate.table = table->name;
        candidate.equality = equality.empty() ? join_equality : equality;
        if (candidate.equality.size() > max_equality_columns) {
            candidate.equality.resize(max_equality_columns);
        }

        const bool drives = util::iequals(facts.tables.front().ref(), table->ref());
        std::vector<std::string> sort;
        if (drives && !facts.order_by_rand && !facts.order_by_expression &&
            !facts.order_by_mixed_directions) {
            for (const auto& c : columns_on(facts.order_by, *table)) {
                if (!contains(candidate.equality, c)) sort.push_back(c);
            }
        }
        std::vector<std::string> group;
        if (drives && facts.order_by.empty()) {
            for (const auto& c : columns_on(facts.group_by, *table)) {
                if (!contains(candidate.equality, c)) group.push_back(c);
            }
        }

        if (!sort.empty() && (range.empty() || util::iequals(range, sort.front()))) {
            candidate.tail = sort;
            candidate.tail_kind = TailKind::Sort;
        } else if (!range.empty()) {
            candidate.tail = {range};
            candidate.tail_kind = TailKind::Range;
        } else if (!group.empty()) {
            candidate.tail = group;
            candidate.tail_kind = TailKind::Group;
        }
        if (candidate.tail.size() > max_tail_columns) candidate.tail.resize(max_tail_columns);
        if (candidate.equality.empty() && candidate.tail.empty()) continue;

        if (catalog != nullptr) {
            if (const schema::Table* t = catalog->find_table(table->name)) {
                candidate.schema_checked = true;
                const auto cols = candidate.columns();
                const bool all_known = std::all_of(cols.begin(), cols.end(), [&](const std::string& c) {
                    return t->find_column(c) != nullptr;
                });
                if (!all_known) continue;
                if (t->index_serving(candidate.equality, candidate.tail) != nullptr) continue;
                if (!candidate.tail.empty() && !candidate.equality.empty()) {
                    if (const schema::Index* i = t->index_serving(candidate.equality, {})) {
                        candidate.partial_index = i->name;
                    }
                }
                for (const schema::Index* i :
                     t->indexes_made_redundant_by(candidate.equality, candidate.tail)) {
                    candidate.redundant.push_back({i->name, i->columns});
                }
            }
        }
        if (!candidate.schema_checked && candidate.tail.empty() && candidate.equality.size() == 1 &&
            util::iequals(candidate.equality.front(), "id")) {
            continue;
        }

        const auto cols = candidate.columns();
        candidate.name = index_name(candidate.table, cols);
        std::vector<std::string> quoted;
        for (const auto& c : cols) quoted.push_back(sql::quote_identifier(c));
        candidate.ddl = std::format("ALTER TABLE {} ADD INDEX {} ({});",
                                    sql::quote_identifier(candidate.table), candidate.name,
                                    util::join(quoted, ", "));

        const bool duplicate = std::any_of(out.begin(), out.end(), [&](const IndexCandidate& c) {
            return util::iequals(c.table, candidate.table) && c.columns() == candidate.columns();
        });
        if (!duplicate) out.push_back(std::move(candidate));
    }
    return out;
}

MissingIndexRule::MissingIndexRule()
    : Rule({"ST001", "missing-index",
            "Suggests the composite index (equality columns, then range or sort) that serves the query",
            Severity::Warning}) {}

void MissingIndexRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    for (const IndexCandidate& c : suggest_indexes(*context.facts, context.catalog)) {
        std::string what;
        if (!c.equality.empty()) {
            what = std::format("filters {} by {}", c.table, join_words(c.equality));
        } else {
            what = std::format("reads {}", c.table);
        }
        switch (c.tail_kind) {
        case TailKind::Sort: what += std::format(" and sorts by {}", join_words(c.tail)); break;
        case TailKind::Range: what += std::format(" with a range on {}", c.tail.front()); break;
        case TailKind::Group: what += std::format(" and groups by {}", join_words(c.tail)); break;
        case TailKind::None: break;
        }

        std::string detail = "The query " + what + ". ";
        const std::string_view tail_word =
            c.tail_kind == TailKind::Sort ? "sort" : (c.tail_kind == TailKind::Group ? "grouping" : "range");
        if (!c.partial_index.empty()) {
            detail += std::format("{} serves the lookup but not the {}, so MySQL {}. ", c.partial_index, tail_word,
                                  c.tail_kind == TailKind::Sort ? "sorts every matching row (filesort)"
                                                                : "reads every matching row");
        } else if (!c.redundant.empty()) {
            const ExistingIndex& existing = c.redundant.front();
            detail += std::format("{} covers only {}, so MySQL reads every row that matches it and checks the "
                                  "rest one by one{}. ",
                                  existing.name, join_words(existing.columns),
                                  c.tail_kind == TailKind::Sort ? ", then sorts them (filesort)" : "");
        }
        if (c.equality.empty()) {
            detail += "An index on the range or sort column lets MySQL read only the rows it needs, in order.";
        } else if (c.tail.empty()) {
            detail += "An index on the equality columns lets MySQL seek straight to the matching rows.";
        } else {
            detail += std::format(
                "Equality columns first, then the {} column{}, lets MySQL seek to the matching rows{}.",
                c.tail_kind == TailKind::Range ? "range" : (c.tail_kind == TailKind::Sort ? "sort" : "grouping"),
                c.tail.size() > 1 ? "s" : "",
                c.tail_kind == TailKind::Range ? " and stop at the end of the range" : " already in order");
        }

        Severity severity = Severity::Warning;
        if (const stats::QueryClass* s = context.stats; s != nullptr && s->calls() > 0) {
            const double examined = s->rows_examined().mean();
            const double returned = std::max(1.0, s->rows_sent().mean());
            detail += std::format(" In the slow log it examined {} rows per execution to return {}",
                                  util::format_compact(examined), util::format_compact(s->rows_sent().mean()));
            const double scans = s->flag_ratio(log::ExecutionFlag::FullScan);
            const double sorts = s->flag_ratio(log::ExecutionFlag::Filesort);
            if (scans > 0.0) detail += std::format("; {:.0f}% of executions scanned a table", scans * 100.0);
            if (sorts > 0.0) detail += std::format("; {:.0f}% sorted with a filesort", sorts * 100.0);
            detail += ".";
            if (examined >= 10'000 && examined / returned >= 1'000) severity = Severity::Critical;
        }
        if (!c.schema_checked) {
            detail += " Existing indexes were not checked: pass the schema (--schema) to verify.";
        }

        std::string suggestion = c.ddl;
        for (const auto& r : c.redundant) {
            suggestion += std::format("\nThen drop the index it makes redundant: ALTER TABLE {} DROP INDEX {};",
                                      sql::quote_identifier(c.table), r.name);
        }
        out.push_back(finding(severity,
                              std::format("{} needs an index on ({})", c.table, util::join(c.columns(), ", ")),
                              std::move(detail), std::move(suggestion)));
    }
}

}
