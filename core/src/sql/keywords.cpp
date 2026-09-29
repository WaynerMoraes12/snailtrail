#include "snailtrail/sql/keywords.hpp"

#include <algorithm>
#include <array>

#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

namespace {

constexpr bool iless(std::string_view a, std::string_view b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char x = util::to_lower(a[i]);
        const char y = util::to_lower(b[i]);
        if (x != y) return x < y;
    }
    return a.size() < b.size();
}

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& sorted, std::string_view word) noexcept {
    if (word.empty() || word.size() > 24) return false;
    return std::binary_search(sorted.begin(), sorted.end(), word, iless);
}

constexpr std::array<std::string_view, 59> reserved = {
    "all",     "and",     "as",       "asc",       "between",   "by",       "case",
    "cross",   "delete",  "desc",     "distinct",  "div",       "else",     "end",
    "except",  "exists",  "for",      "force",     "from",      "full",     "group",
    "having",  "ignore",  "in",       "inner",     "insert",    "intersect", "into",
    "is",      "join",    "key",      "left",      "like",      "limit",    "lock",
    "mod",     "natural", "not",      "offset",    "on",        "or",       "order",
    "outer",   "partition", "regexp", "right",     "rlike",     "select",   "set",
    "straight_join", "then", "union", "update",    "use",       "using",    "values",
    "when",    "where",   "window",
};

constexpr std::array<std::string_view, 46> operator_keywords = {
    "all",       "and",    "any",    "as",       "between",   "by",     "case",
    "distinct",  "div",    "else",   "exists",   "from",      "having", "in",
    "interval",  "into",   "is",     "join",     "like",      "limit",  "mod",
    "not",       "offset", "on",     "or",       "over",      "partition", "recursive",
    "regexp",    "return", "rlike",  "rows",     "select",    "set",    "some",
    "straight_join", "then", "union", "using",   "value",     "values", "when",
    "where",     "window", "with",   "xor",
};

constexpr std::array<std::string_view, 21> aggregates = {
    "any_value", "approx_count_distinct", "avg",       "bit_and",        "bit_or",
    "bit_xor",   "count",                 "count_big", "group_concat",   "json_arrayagg",
    "json_objectagg", "max",              "min",       "std",            "stddev",
    "stddev_pop", "stddev_samp",          "sum",       "var_pop",        "var_samp",
    "variance",
};

static_assert(std::is_sorted(reserved.begin(), reserved.end(), iless));
static_assert(std::is_sorted(operator_keywords.begin(), operator_keywords.end(), iless));
static_assert(std::is_sorted(aggregates.begin(), aggregates.end(), iless));

}

bool is_reserved_word(std::string_view word) noexcept { return contains(reserved, word); }

bool is_operator_keyword(std::string_view word) noexcept {
    return contains(operator_keywords, word);
}

bool is_aggregate_function(std::string_view name) noexcept { return contains(aggregates, name); }

}
