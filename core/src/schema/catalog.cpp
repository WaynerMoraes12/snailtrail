#include "snailtrail/schema/catalog.hpp"

#include <algorithm>
#include <format>

#include "snailtrail/sql/parser.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::schema {

namespace {

bool contains_icase(std::span<const std::string> set, std::string_view value) {
    return std::any_of(set.begin(), set.end(),
                       [&](const std::string& s) { return util::iequals(s, value); });
}

std::string_view base_type(std::string_view type) noexcept {
    const std::size_t end = type.find_first_of("( ");
    return end == std::string_view::npos ? type : type.substr(0, end);
}

}

TypeCategory categorize_type(std::string_view type) noexcept {
    const std::string_view base = base_type(util::trim(type));
    auto is = [&](std::initializer_list<std::string_view> names) {
        return std::any_of(names.begin(), names.end(),
                           [&](std::string_view n) { return util::iequals(base, n); });
    };
    if (is({"tinyint", "smallint", "mediumint", "int", "integer", "bigint", "bit", "bool",
            "boolean", "serial", "year"})) {
        return TypeCategory::Integer;
    }
    if (is({"decimal", "numeric", "dec", "fixed"})) return TypeCategory::Decimal;
    if (is({"float", "double", "real"})) return TypeCategory::Float;
    if (is({"char", "varchar", "tinytext", "text", "mediumtext", "longtext", "enum", "set",
            "nchar", "nvarchar"})) {
        return TypeCategory::String;
    }
    if (is({"binary", "varbinary", "tinyblob", "blob", "mediumblob", "longblob"})) {
        return TypeCategory::Binary;
    }
    if (is({"date", "datetime", "timestamp", "time"})) return TypeCategory::Temporal;
    if (is({"json"})) return TypeCategory::Json;
    return TypeCategory::Other;
}

std::string_view type_category_name(TypeCategory category) noexcept {
    switch (category) {
    case TypeCategory::Integer: return "integer";
    case TypeCategory::Decimal: return "decimal";
    case TypeCategory::Float: return "float";
    case TypeCategory::String: return "string";
    case TypeCategory::Binary: return "binary";
    case TypeCategory::Temporal: return "temporal";
    case TypeCategory::Json: return "json";
    case TypeCategory::Other: return "other";
    }
    return "other";
}

bool Index::is_btree() const noexcept {
    return kind == sql::IndexKind::Primary || kind == sql::IndexKind::Unique ||
           kind == sql::IndexKind::Regular;
}

bool Index::serves(std::span<const std::string> equality, std::span<const std::string> tail) const {
    if (!is_btree() || columns.size() < equality.size() + tail.size()) return false;
    for (std::size_t i = 0; i < equality.size(); ++i) {
        if (!contains_icase(equality, columns[i])) return false;
    }
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (!util::iequals(columns[equality.size() + i], tail[i])) return false;
    }
    return !equality.empty() || !tail.empty();
}

bool Index::is_prefix_of(std::span<const std::string> equality,
                         std::span<const std::string> tail) const {
    const std::size_t wanted = equality.size() + tail.size();
    if (!is_btree() || columns.empty() || columns.size() >= wanted) return false;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i < equality.size()) {
            if (!contains_icase(equality, columns[i])) return false;
        } else if (!util::iequals(columns[i], tail[i - equality.size()])) {
            return false;
        }
    }
    return true;
}

Table::Table(std::string name, std::string schema)
    : name_(std::move(name)), schema_(std::move(schema)) {}

const Column* Table::find_column(std::string_view name) const noexcept {
    for (const Column& c : columns_) {
        if (util::iequals(c.name, name)) return &c;
    }
    return nullptr;
}

const Index* Table::find_index(std::string_view name) const noexcept {
    for (const Index& i : indexes_) {
        if (util::iequals(i.name, name)) return &i;
    }
    return nullptr;
}

const Index* Table::primary_key() const noexcept {
    for (const Index& i : indexes_) {
        if (i.kind == sql::IndexKind::Primary) return &i;
    }
    return nullptr;
}

const Index* Table::index_serving(std::span<const std::string> equality,
                                  std::span<const std::string> tail) const {
    for (const Index& i : indexes_) {
        if (i.serves(equality, tail)) return &i;
    }
    return nullptr;
}

std::vector<const Index*> Table::indexes_made_redundant_by(std::span<const std::string> equality,
                                                           std::span<const std::string> tail) const {
    std::vector<const Index*> out;
    for (const Index& i : indexes_) {
        if (i.kind == sql::IndexKind::Regular && i.is_prefix_of(equality, tail)) out.push_back(&i);
    }
    return out;
}

void Table::add_column(Column column) {
    for (Column& c : columns_) {
        if (util::iequals(c.name, column.name)) {
            c = std::move(column);
            return;
        }
    }
    columns_.push_back(std::move(column));
}

void Table::add_index(Index index) {
    if (index.name.empty() && !index.columns.empty()) index.name = index.columns.front();
    for (Index& i : indexes_) {
        if (util::iequals(i.name, index.name)) {
            i = std::move(index);
            return;
        }
    }
    indexes_.push_back(std::move(index));
}

bool Table::drop_index(std::string_view name) {
    const auto it = std::find_if(indexes_.begin(), indexes_.end(),
                                 [&](const Index& i) { return util::iequals(i.name, name); });
    if (it == indexes_.end()) return false;
    indexes_.erase(it);
    return true;
}

namespace {

Column make_column(const sql::ColumnDefinition& d) {
    return Column{d.name, d.type, categorize_type(d.type), d.nullable};
}

Index make_index(const sql::IndexDefinition& d) { return Index{d.name, d.kind, d.columns}; }

class CatalogBuilder final : public sql::StatementVisitor {
public:
    explicit CatalogBuilder(SchemaCatalog& catalog) : catalog_(catalog) {}

    void visit(const sql::CreateTableStatement& s) override {
        Table table(s.table().name, s.table().schema);
        for (const auto& c : s.columns()) table.add_column(make_column(c));
        for (const auto& i : s.indexes()) table.add_index(make_index(i));
        catalog_.add_table(std::move(table));
    }

    void visit(const sql::AlterTableStatement& s) override {
        const Table* existing = catalog_.find_table(s.table().name);
        Table table = existing != nullptr ? *existing : Table(s.table().name, s.table().schema);
        for (const auto& c : s.added_columns()) table.add_column(make_column(c));
        for (const auto& name : s.dropped_indexes()) table.drop_index(name);
        for (const auto& i : s.added_indexes()) table.add_index(make_index(i));
        catalog_.add_table(std::move(table));
    }

    void visit(const sql::SelectStatement&) override {}
    void visit(const sql::InsertStatement&) override {}
    void visit(const sql::UpdateStatement&) override {}
    void visit(const sql::DeleteStatement&) override {}
    void visit(const sql::OtherStatement&) override {}

private:
    SchemaCatalog& catalog_;
};

}

SchemaCatalog SchemaCatalog::from_ddl(std::string_view ddl, std::vector<std::string>* warnings) {
    SchemaCatalog catalog;
    sql::Parser parser(ddl);
    std::vector<sql::ParseError> errors;
    for (const auto& statement : parser.parse_script(&errors)) catalog.apply(*statement);
    if (warnings != nullptr) {
        for (const auto& e : errors) {
            warnings->push_back(std::format("skipped a statement: {}", e.what()));
        }
    }
    return catalog;
}

void SchemaCatalog::apply(const sql::Statement& statement) {
    CatalogBuilder builder(*this);
    statement.accept(builder);
}

Table& SchemaCatalog::add_table(Table table) {
    std::string key = util::to_lower(table.name());
    return tables_.insert_or_assign(std::move(key), std::move(table)).first->second;
}

const Table* SchemaCatalog::find_table(std::string_view name) const {
    const auto it = tables_.find(util::to_lower(name));
    return it == tables_.end() ? nullptr : &it->second;
}

std::vector<const Table*> SchemaCatalog::tables_with_column(std::string_view column) const {
    std::vector<const Table*> out;
    for (const auto& [key, table] : tables_) {
        if (table.find_column(column) != nullptr) out.push_back(&table);
    }
    return out;
}

std::vector<const Table*> SchemaCatalog::tables() const {
    std::vector<const Table*> out;
    out.reserve(tables_.size());
    for (const auto& [key, table] : tables_) out.push_back(&table);
    return out;
}

}
