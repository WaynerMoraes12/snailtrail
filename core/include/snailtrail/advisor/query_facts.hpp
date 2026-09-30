#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/sql/ast.hpp"

namespace snailtrail::advisor {

struct ColumnUse {
    std::string table;
    std::string ref;
    std::string column;

    [[nodiscard]] bool resolved() const noexcept { return !table.empty(); }
    [[nodiscard]] std::string display() const;
    [[nodiscard]] bool same_as(const ColumnUse& other) const noexcept;
};

enum class PredicateKind : std::uint8_t { Equality, In, Range, Like, IsNull, Inequality, Other };

enum class ValueKind : std::uint8_t { None, Number, String, Constant, Column, Subquery };

struct Predicate {
    PredicateKind kind = PredicateKind::Other;
    ColumnUse column;
    std::optional<ColumnUse> other;
    std::string wrapper;
    std::string expression_sql;
    ValueKind value = ValueKind::None;
    std::string value_sql;
    std::string like_pattern;
    std::size_t in_list_size = 0;
    bool negated = false;
    bool under_or = false;
    bool from_join = false;

    [[nodiscard]] bool is_join() const noexcept { return other.has_value(); }
    [[nodiscard]] bool sargable() const noexcept { return wrapper.empty() && !negated; }
};

struct TableUse {
    std::string name;
    std::string alias;
    bool derived = false;
    std::string schema;

    [[nodiscard]] const std::string& ref() const noexcept { return alias.empty() ? name : alias; }
    [[nodiscard]] bool in_system_schema() const noexcept;
};

struct OrGroup {
    std::vector<ColumnUse> columns;
    std::string sql;
};

struct JoinLink {
    std::string left_ref;
    std::string right_ref;
};

struct QueryFacts {
    sql::StatementKind kind = sql::StatementKind::Unknown;
    std::vector<TableUse> tables;
    std::vector<Predicate> predicates;
    std::vector<OrGroup> or_groups;
    std::vector<JoinLink> links;
    std::vector<ColumnUse> order_by;
    std::vector<ColumnUse> group_by;
    std::optional<std::uint64_t> limit;
    std::optional<std::uint64_t> offset;
    std::vector<std::string> star_tables;
    bool has_where = false;
    bool select_star = false;
    bool order_by_rand = false;
    bool order_by_expression = false;
    bool order_by_mixed_directions = false;
    bool order_by_descending = false;
    bool has_aggregate = false;
    bool having_without_aggregate = false;
    bool not_in_subquery = false;
    bool explicit_cross_join = false;
    bool unresolved_join_column = false;
    std::size_t max_in_list = 0;

    [[nodiscard]] const TableUse* find_table(std::string_view ref) const noexcept;
    [[nodiscard]] std::vector<const TableUse*> base_tables() const;
};

QueryFacts collect_facts(const sql::Statement& statement, const schema::SchemaCatalog* catalog);

}
