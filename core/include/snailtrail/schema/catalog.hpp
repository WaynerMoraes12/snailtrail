#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/sql/ast.hpp"

namespace snailtrail::schema {

enum class TypeCategory : std::uint8_t {
    Integer,
    Decimal,
    Float,
    String,
    Binary,
    Temporal,
    Json,
    Other,
};

TypeCategory categorize_type(std::string_view type) noexcept;
std::string_view type_category_name(TypeCategory category) noexcept;

constexpr bool is_numeric(TypeCategory c) noexcept {
    return c == TypeCategory::Integer || c == TypeCategory::Decimal || c == TypeCategory::Float;
}

constexpr bool is_textual(TypeCategory c) noexcept {
    return c == TypeCategory::String || c == TypeCategory::Binary;
}

struct Column {
    std::string name;
    std::string type;
    TypeCategory category = TypeCategory::Other;
    bool nullable = true;
};

struct Index {
    std::string name;
    sql::IndexKind kind = sql::IndexKind::Regular;
    std::vector<std::string> columns;

    [[nodiscard]] bool is_btree() const noexcept;
    [[nodiscard]] bool serves(std::span<const std::string> equality,
                              std::span<const std::string> tail) const;
    [[nodiscard]] bool is_prefix_of(std::span<const std::string> equality,
                                    std::span<const std::string> tail) const;
};

class Table {
public:
    Table() = default;
    explicit Table(std::string name, std::string schema = {});

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& schema() const noexcept { return schema_; }
    [[nodiscard]] const std::vector<Column>& columns() const noexcept { return columns_; }
    [[nodiscard]] const std::vector<Index>& indexes() const noexcept { return indexes_; }

    [[nodiscard]] const Column* find_column(std::string_view name) const noexcept;
    [[nodiscard]] const Index* find_index(std::string_view name) const noexcept;
    [[nodiscard]] const Index* primary_key() const noexcept;

    [[nodiscard]] const Index* index_serving(std::span<const std::string> equality,
                                             std::span<const std::string> tail) const;
    [[nodiscard]] std::vector<const Index*> indexes_made_redundant_by(
        std::span<const std::string> equality, std::span<const std::string> tail) const;

    void add_column(Column column);
    void add_index(Index index);
    bool drop_index(std::string_view name);

private:
    std::string name_;
    std::string schema_;
    std::vector<Column> columns_;
    std::vector<Index> indexes_;
};

class SchemaCatalog {
public:
    static SchemaCatalog from_ddl(std::string_view ddl, std::vector<std::string>* warnings = nullptr);

    void apply(const sql::Statement& statement);
    Table& add_table(Table table);

    [[nodiscard]] const Table* find_table(std::string_view name) const;
    [[nodiscard]] std::vector<const Table*> tables_with_column(std::string_view column) const;
    [[nodiscard]] std::vector<const Table*> tables() const;
    [[nodiscard]] std::size_t size() const noexcept { return tables_.size(); }
    [[nodiscard]] bool empty() const noexcept { return tables_.empty(); }

private:
    std::map<std::string, Table> tables_;
};

}
