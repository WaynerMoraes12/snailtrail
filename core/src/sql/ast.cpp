#include "snailtrail/sql/ast.hpp"

#include "snailtrail/sql/keywords.hpp"
#include "snailtrail/sql/token.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

Literal::Literal(LiteralKind kind, std::string text) : kind_(kind), text_(std::move(text)) {}

std::string Literal::value() const {
    return kind_ == LiteralKind::String ? unquote_string(text_) : text_;
}

std::optional<double> Literal::number() const {
    if (kind_ != LiteralKind::Number) return std::nullopt;
    return util::parse_double(text_);
}

bool Literal::is_constant() const noexcept {
    return kind_ != LiteralKind::Variable && kind_ != LiteralKind::Keyword;
}

void Literal::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

ColumnRef::ColumnRef(std::string schema, std::string table, std::string name)
    : schema_(std::move(schema)), table_(std::move(table)), name_(std::move(name)) {}

std::string ColumnRef::qualified_name() const {
    std::string out;
    if (!schema_.empty()) out += schema_ + ".";
    if (!table_.empty()) out += table_ + ".";
    return out + name_;
}

void ColumnRef::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

UnaryExpr::UnaryExpr(UnaryOp op, ExprPtr operand) : op_(op), operand_(std::move(operand)) {}

void UnaryExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

std::string_view binary_op_symbol(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Or: return "OR";
    case BinaryOp::Xor: return "XOR";
    case BinaryOp::And: return "AND";
    case BinaryOp::Eq: return "=";
    case BinaryOp::NullSafeEq: return "<=>";
    case BinaryOp::NotEq: return "<>";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEq: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEq: return ">=";
    case BinaryOp::Like: return "LIKE";
    case BinaryOp::NotLike: return "NOT LIKE";
    case BinaryOp::Regexp: return "REGEXP";
    case BinaryOp::NotRegexp: return "NOT REGEXP";
    case BinaryOp::BitOr: return "|";
    case BinaryOp::BitAnd: return "&";
    case BinaryOp::ShiftLeft: return "<<";
    case BinaryOp::ShiftRight: return ">>";
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::IntDiv: return "DIV";
    case BinaryOp::Mod: return "%";
    case BinaryOp::BitXor: return "^";
    case BinaryOp::JsonExtract: return "->";
    case BinaryOp::JsonUnquoteExtract: return "->>";
    case BinaryOp::Assign: return ":=";
    }
    return "?";
}

bool is_comparison(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Eq:
    case BinaryOp::NullSafeEq:
    case BinaryOp::NotEq:
    case BinaryOp::Less:
    case BinaryOp::LessEq:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEq:
    case BinaryOp::Like:
    case BinaryOp::NotLike:
    case BinaryOp::Regexp:
    case BinaryOp::NotRegexp: return true;
    default: return false;
    }
}

bool is_logical(BinaryOp op) noexcept {
    return op == BinaryOp::And || op == BinaryOp::Or || op == BinaryOp::Xor;
}

bool is_arithmetic(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Add:
    case BinaryOp::Sub:
    case BinaryOp::Mul:
    case BinaryOp::Div:
    case BinaryOp::IntDiv:
    case BinaryOp::Mod: return true;
    default: return false;
    }
}

int precedence(BinaryOp op) noexcept {
    switch (op) {
    case BinaryOp::Assign: return 1;
    case BinaryOp::Or: return 2;
    case BinaryOp::Xor: return 3;
    case BinaryOp::And: return 4;
    case BinaryOp::Eq:
    case BinaryOp::NullSafeEq:
    case BinaryOp::NotEq:
    case BinaryOp::Less:
    case BinaryOp::LessEq:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEq:
    case BinaryOp::Like:
    case BinaryOp::NotLike:
    case BinaryOp::Regexp:
    case BinaryOp::NotRegexp: return 6;
    case BinaryOp::BitOr: return 7;
    case BinaryOp::BitAnd: return 8;
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight: return 9;
    case BinaryOp::Add:
    case BinaryOp::Sub: return 10;
    case BinaryOp::Mul:
    case BinaryOp::Div:
    case BinaryOp::IntDiv:
    case BinaryOp::Mod: return 11;
    case BinaryOp::BitXor: return 12;
    case BinaryOp::JsonExtract:
    case BinaryOp::JsonUnquoteExtract: return 14;
    }
    return 0;
}

BinaryExpr::BinaryExpr(BinaryOp op, ExprPtr left, ExprPtr right)
    : op_(op), left_(std::move(left)), right_(std::move(right)) {}

void BinaryExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

InExpr::InExpr(ExprPtr operand, std::vector<ExprPtr> values, bool negated)
    : operand_(std::move(operand)), values_(std::move(values)), negated_(negated) {}

InExpr::InExpr(ExprPtr operand, std::unique_ptr<SelectStatement> subquery, bool negated)
    : operand_(std::move(operand)), subquery_(std::move(subquery)), negated_(negated) {}

InExpr::~InExpr() = default;

void InExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

BetweenExpr::BetweenExpr(ExprPtr operand, ExprPtr low, ExprPtr high, bool negated)
    : operand_(std::move(operand)), low_(std::move(low)), high_(std::move(high)),
      negated_(negated) {}

void BetweenExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

IsExpr::IsExpr(ExprPtr operand, IsTest test, bool negated)
    : operand_(std::move(operand)), test_(test), negated_(negated) {}

void IsExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

FunctionCall::FunctionCall(std::string name, std::vector<ExprPtr> args, bool star, bool distinct,
                           bool windowed)
    : name_(std::move(name)), args_(std::move(args)), star_(star), distinct_(distinct),
      windowed_(windowed) {}

bool FunctionCall::is_aggregate() const noexcept {
    return !windowed_ && is_aggregate_function(name_);
}

void FunctionCall::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

CaseExpr::CaseExpr(ExprPtr operand, std::vector<WhenClause> branches, ExprPtr else_result)
    : operand_(std::move(operand)), branches_(std::move(branches)),
      else_result_(std::move(else_result)) {}

void CaseExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

CastExpr::CastExpr(ExprPtr operand, std::string target_type)
    : operand_(std::move(operand)), target_type_(std::move(target_type)) {}

void CastExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

IntervalExpr::IntervalExpr(ExprPtr value, std::string unit)
    : value_(std::move(value)), unit_(std::move(unit)) {}

void IntervalExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

SubqueryExpr::SubqueryExpr(std::unique_ptr<SelectStatement> query, Quantifier quantifier)
    : query_(std::move(query)), quantifier_(quantifier) {}

SubqueryExpr::~SubqueryExpr() = default;

void SubqueryExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

RowExpr::RowExpr(std::vector<ExprPtr> items) : items_(std::move(items)) {}

void RowExpr::accept(ExprVisitor& visitor) const { visitor.visit(*this); }

void RecursiveExprVisitor::visit(const Literal&) {}

void RecursiveExprVisitor::visit(const ColumnRef&) {}

void RecursiveExprVisitor::visit(const UnaryExpr& e) { e.operand().accept(*this); }

void RecursiveExprVisitor::visit(const BinaryExpr& e) {
    e.left().accept(*this);
    e.right().accept(*this);
}

void RecursiveExprVisitor::visit(const InExpr& e) {
    e.operand().accept(*this);
    for (const auto& v : e.values()) v->accept(*this);
}

void RecursiveExprVisitor::visit(const BetweenExpr& e) {
    e.operand().accept(*this);
    e.low().accept(*this);
    e.high().accept(*this);
}

void RecursiveExprVisitor::visit(const IsExpr& e) { e.operand().accept(*this); }

void RecursiveExprVisitor::visit(const FunctionCall& e) {
    for (const auto& a : e.args()) a->accept(*this);
}

void RecursiveExprVisitor::visit(const CaseExpr& e) {
    if (e.operand() != nullptr) e.operand()->accept(*this);
    for (const auto& b : e.branches()) {
        b.condition->accept(*this);
        b.result->accept(*this);
    }
    if (e.else_result() != nullptr) e.else_result()->accept(*this);
}

void RecursiveExprVisitor::visit(const CastExpr& e) { e.operand().accept(*this); }

void RecursiveExprVisitor::visit(const IntervalExpr& e) { e.value().accept(*this); }

void RecursiveExprVisitor::visit(const SubqueryExpr&) {}

void RecursiveExprVisitor::visit(const RowExpr& e) {
    for (const auto& i : e.items()) i->accept(*this);
}

TableRef::TableRef() = default;
TableRef::TableRef(TableRef&&) noexcept = default;
TableRef& TableRef::operator=(TableRef&&) noexcept = default;
TableRef::~TableRef() = default;

CommonTableExpr::CommonTableExpr() = default;
CommonTableExpr::CommonTableExpr(CommonTableExpr&&) noexcept = default;
CommonTableExpr& CommonTableExpr::operator=(CommonTableExpr&&) noexcept = default;
CommonTableExpr::~CommonTableExpr() = default;

SetBranch::SetBranch() = default;
SetBranch::SetBranch(SetBranch&&) noexcept = default;
SetBranch& SetBranch::operator=(SetBranch&&) noexcept = default;
SetBranch::~SetBranch() = default;

std::string_view join_kind_name(JoinKind kind) noexcept {
    switch (kind) {
    case JoinKind::Inner: return "JOIN";
    case JoinKind::Left: return "LEFT JOIN";
    case JoinKind::Right: return "RIGHT JOIN";
    case JoinKind::Cross: return "CROSS JOIN";
    case JoinKind::Straight: return "STRAIGHT_JOIN";
    }
    return "JOIN";
}

std::string_view set_operator_name(SetOperator op) noexcept {
    switch (op) {
    case SetOperator::Union: return "UNION";
    case SetOperator::UnionAll: return "UNION ALL";
    case SetOperator::Except: return "EXCEPT";
    case SetOperator::Intersect: return "INTERSECT";
    }
    return "UNION";
}

std::string_view index_kind_name(IndexKind kind) noexcept {
    switch (kind) {
    case IndexKind::Primary: return "PRIMARY";
    case IndexKind::Unique: return "UNIQUE";
    case IndexKind::Regular: return "INDEX";
    case IndexKind::Fulltext: return "FULLTEXT";
    case IndexKind::Spatial: return "SPATIAL";
    }
    return "INDEX";
}

void SelectStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }
void InsertStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }
void UpdateStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }
void DeleteStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }
void CreateTableStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }
void AlterTableStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }

OtherStatement::OtherStatement(StatementKind kind, std::string keyword)
    : kind_(kind), keyword_(std::move(keyword)) {}

void OtherStatement::accept(StatementVisitor& visitor) const { visitor.visit(*this); }

}
