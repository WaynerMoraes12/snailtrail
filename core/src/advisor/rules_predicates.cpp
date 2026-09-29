#include <algorithm>
#include <format>
#include <set>

#include "helpers.hpp"
#include "snailtrail/advisor/index_advisor.hpp"
#include "snailtrail/advisor/rules.hpp"
#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

using detail::join_words;

namespace {

bool is_date_function(std::string_view f) {
    static constexpr std::string_view names[] = {"date",      "year",       "month",   "day",
                                                 "dayofmonth", "date_format", "to_days", "week",
                                                 "yearweek",  "unix_timestamp", "hour", "time"};
    return std::any_of(std::begin(names), std::end(names),
                       [&](std::string_view n) { return util::iequals(f, n); });
}

std::string range_rewrite(const Predicate& p) {
    const std::string& col = p.column.column;
    if (util::iequals(p.wrapper, "date") && p.kind == PredicateKind::Equality) {
        return std::format("{0} >= {1} AND {0} < {1} + INTERVAL 1 DAY", col, p.value_sql);
    }
    if (util::iequals(p.wrapper, "date") && p.kind == PredicateKind::Range) {
        return std::format("compare {} itself with the same bound: DATE(x) >= d is x >= d, DATE(x) <= d is "
                           "x < d + INTERVAL 1 DAY",
                           col);
    }
    if (util::iequals(p.wrapper, "year") && p.kind == PredicateKind::Equality && p.value == ValueKind::Number) {
        if (auto year = util::parse_uint(p.value_sql)) {
            return std::format("{0} >= '{1}-01-01' AND {0} < '{2}-01-01'", col, *year, *year + 1);
        }
    }
    return std::format("compare the bare {} with a range computed from the value", col);
}

}

NonSargablePredicateRule::NonSargablePredicateRule()
    : Rule({"ST004", "non-sargable-predicate", "Indexed columns wrapped in functions or arithmetic",
            Severity::Warning}) {}

void NonSargablePredicateRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    std::set<std::string> seen;
    for (const Predicate& p : context.facts->predicates) {
        if (p.wrapper.empty() || p.is_join() || p.value == ValueKind::Column) continue;
        if (p.kind != PredicateKind::Equality && p.kind != PredicateKind::In &&
            p.kind != PredicateKind::Range && p.kind != PredicateKind::Like) {
            continue;
        }
        if (util::iequals(p.wrapper, "match") || !seen.insert(p.expression_sql).second) continue;

        const std::string col = p.column.display();
        std::string suggestion;
        if (is_date_function(p.wrapper)) {
            suggestion = "Rewrite as a range on the bare column: " + range_rewrite(p) + ".";
        } else if (util::iequals(p.wrapper, "lower") || util::iequals(p.wrapper, "upper")) {
            suggestion = std::format(
                "With a case-insensitive collation (MySQL's default, e.g. utf8mb4_0900_ai_ci) `{} = ...` "
                "already ignores case: drop {}() and the index on {} applies.",
                p.column.column, util::to_upper(p.wrapper), p.column.column);
            if (detail::leads_an_index(context.catalog, p.column)) {
                suggestion += std::format(" ({} already has one.)", col);
            } else if (p.column.resolved()) {
                suggestion += std::format(
                    "\nFor a case-sensitive collation, index the expression (MySQL 8.0.13+):\n"
                    "ALTER TABLE {} ADD INDEX {} (({}));",
                    sql::quote_identifier(p.column.table),
                    index_name(p.column.table, {p.wrapper, p.column.column}), p.expression_sql);
            }
        } else if (util::iequals(p.wrapper, "arithmetic")) {
            suggestion = std::format("Move the arithmetic to the other side of the comparison so {} stands "
                                     "alone (e.g. price * 1.1 > 100 becomes price > 100 / 1.1).",
                                     p.column.column);
        } else if (util::iequals(p.wrapper, "cast") || util::iequals(p.wrapper, "convert")) {
            suggestion = std::format("Compare {} with a value of its own type instead of converting the column.",
                                     p.column.column);
        } else if (p.column.resolved()) {
            suggestion = std::format("Index the expression itself (MySQL 8.0.13+):\nALTER TABLE {} ADD INDEX {} "
                                     "(({}));",
                                     sql::quote_identifier(p.column.table),
                                     index_name(p.column.table, {p.wrapper, p.column.column}),
                                     p.expression_sql);
        }
        out.push_back(finding(
            Severity::Warning, std::format("{} hides {} from its index", p.expression_sql, col),
            std::format("An index stores {} itself, not {}. Wrapped in {}, the column has to be computed "
                        "for every row, so no index can serve this condition (it is not sargable).",
                        p.column.column, p.expression_sql,
                        util::iequals(p.wrapper, "arithmetic") ? "arithmetic" : "a function"),
            std::move(suggestion)));
    }
}

ImplicitConversionRule::ImplicitConversionRule()
    : Rule({"ST005", "implicit-conversion", "String columns compared with numbers",
            Severity::Warning, true, true}) {}

void ImplicitConversionRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    std::set<std::string> seen;
    for (const Predicate& p : context.facts->predicates) {
        const schema::Column* column = detail::find_column(context.catalog, p.column);
        if (column == nullptr || !p.wrapper.empty()) continue;

        if (p.is_join()) {
            const schema::Column* other = detail::find_column(context.catalog, *p.other);
            if (other == nullptr) continue;
            const bool mismatch = (schema::is_textual(column->category) && schema::is_numeric(other->category)) ||
                                  (schema::is_numeric(column->category) && schema::is_textual(other->category));
            if (!mismatch || !seen.insert(p.column.display() + "=" + p.other->display()).second) continue;
            out.push_back(finding(
                Severity::Warning,
                std::format("{} ({}) is joined to {} ({})", p.column.display(), util::to_upper(column->type),
                            p.other->display(), util::to_upper(other->type)),
                "Joining a string column to a numeric one makes MySQL convert both to floating point "
                "for every pair it compares, so neither column's index can drive the join.",
                "Give both columns the same type (and the same character set and collation for strings)."));
            continue;
        }

        if (p.value != ValueKind::Number || !schema::is_textual(column->category)) continue;
        if (p.kind != PredicateKind::Equality && p.kind != PredicateKind::In && p.kind != PredicateKind::Range) {
            continue;
        }
        if (!seen.insert(p.column.display()).second) continue;

        std::string suggestion = std::format("Quote the value: {} = '{}'", p.column.column, p.value_sql);
        if (!detail::leads_an_index(context.catalog, p.column)) {
            suggestion += std::format("\nThen index the column:\nALTER TABLE {} ADD INDEX {} ({});",
                                      sql::quote_identifier(p.column.table),
                                      index_name(p.column.table, {p.column.column}),
                                      sql::quote_identifier(p.column.column));
        }
        out.push_back(finding(
            Severity::Warning,
            std::format("{} is {} but is compared to a number", p.column.display(), util::to_upper(column->type)),
            std::format("MySQL has to convert every {0} value to a number before comparing, so no index on "
                        "{0} can be used and every execution scans {1}. The conversion is also lossy: "
                        "'0123', '123' and '123abc' all compare equal to 123.",
                        p.column.column, p.column.table),
            std::move(suggestion)));
    }
}

LeadingWildcardRule::LeadingWildcardRule()
    : Rule({"ST006", "leading-wildcard", "LIKE patterns that start with a wildcard", Severity::Warning}) {}

void LeadingWildcardRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    std::set<std::string> seen;
    for (const Predicate& p : context.facts->predicates) {
        if (p.kind != PredicateKind::Like || p.like_pattern.empty() || !p.wrapper.empty()) continue;
        const char first = p.like_pattern.front();
        if (first != '%' && first != '_') continue;
        if (!seen.insert(p.column.display()).second) continue;

        std::string word = p.like_pattern;
        std::erase(word, '%');
        std::erase(word, '_');
        const bool contains_search = p.like_pattern.size() > 1 && p.like_pattern.back() == '%';
        std::string suggestion;
        if (contains_search && p.column.resolved()) {
            suggestion = std::format(
                "For word search, add a FULLTEXT index and use MATCH ... AGAINST:\n"
                "ALTER TABLE {0} ADD FULLTEXT INDEX ft_{1}_{2} ({3});\n"
                "... WHERE MATCH({3}) AGAINST('{4}')",
                sql::quote_identifier(p.column.table), util::to_lower(p.column.table),
                util::to_lower(p.column.column), p.column.column, word);
        } else if (contains_search) {
            suggestion = "For word search, use a FULLTEXT index with MATCH ... AGAINST.";
        } else {
            suggestion = std::format("For suffix search, store REVERSE({0}) in an indexed column and search "
                                     "it with a prefix: reversed_{0} LIKE '{1}%'.",
                                     p.column.column, std::string(word.rbegin(), word.rend()));
        }
        out.push_back(finding(
            Severity::Warning, std::format("LIKE '{}' cannot use an index on {}", p.like_pattern, p.column.display()),
            "A B-tree index is ordered by the start of the value; a pattern that starts with a wildcard "
            "has no start to seek to, so MySQL compares every row.",
            std::move(suggestion)));
    }
}

OrAcrossColumnsRule::OrAcrossColumnsRule()
    : Rule({"ST010", "or-across-columns", "OR conditions on different columns", Severity::Info}) {}

void OrAcrossColumnsRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    for (const OrGroup& group : context.facts->or_groups) {
        if (group.columns.size() < 2) continue;
        std::vector<std::string> names;
        std::vector<std::string> unindexed;
        for (const auto& c : group.columns) {
            names.push_back(c.column);
            if (context.catalog != nullptr && c.resolved() && !detail::leads_an_index(context.catalog, c)) {
                unindexed.push_back(c.column);
            }
        }
        std::string detail =
            "A single index cannot serve both sides of an OR. MySQL either merges separate index scans "
            "(index_merge, only when every column leads an index) or scans the table.";
        if (!unindexed.empty()) {
            detail += std::format(" {} {} no index, so this query scans.", join_words(unindexed),
                                  unindexed.size() == 1 ? "has" : "have");
        }
        out.push_back(finding(
            unindexed.empty() ? Severity::Info : Severity::Warning,
            std::format("OR across {}", join_words(names)), std::move(detail),
            std::format("Rewrite as a UNION of indexed lookups (UNION ALL when the branches cannot overlap):\n"
                        "SELECT ... WHERE {} = ? UNION SELECT ... WHERE {} = ?",
                        names[0], names[1])));
    }
}

}
