#pragma once

#include <string>
#include <string_view>

#include "snailtrail/sql/ast.hpp"

namespace snailtrail::sql {

std::string to_sql(const Expr& expr);
std::string to_sql(const SelectStatement& select);
std::string to_sql(const TableRef& table);

std::string quote_identifier(std::string_view name);

}
