#pragma once

#include <string_view>

namespace snailtrail::sql {

bool is_reserved_word(std::string_view word) noexcept;
bool is_operator_keyword(std::string_view word) noexcept;
bool is_aggregate_function(std::string_view name) noexcept;

}
