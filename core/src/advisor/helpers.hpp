#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/sql/ast.hpp"

namespace snailtrail::advisor::detail {

const sql::Expr* where_of(const sql::Statement& statement);
const sql::SelectStatement* select_of(const sql::Statement& statement);

std::string join_words(const std::vector<std::string>& words);
std::string singular(std::string_view name);
std::string strip_quotes(std::string_view literal);

const schema::Column* find_column(const schema::SchemaCatalog* catalog, const ColumnUse& use);
bool leads_an_index(const schema::SchemaCatalog* catalog, const ColumnUse& use);

}
