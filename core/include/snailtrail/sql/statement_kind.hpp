#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "snailtrail/sql/token.hpp"

namespace snailtrail::sql {

enum class StatementKind : std::uint8_t {
    Select,
    Insert,
    Update,
    Delete,
    Replace,
    Call,
    Ddl,
    Transaction,
    Utility,
    Unknown,
};

std::string_view statement_kind_name(StatementKind kind) noexcept;

StatementKind classify_statement(std::span<const Token> tokens) noexcept;

constexpr bool is_dml(StatementKind kind) noexcept {
    return kind == StatementKind::Select || kind == StatementKind::Insert ||
           kind == StatementKind::Update || kind == StatementKind::Delete ||
           kind == StatementKind::Replace;
}

}
