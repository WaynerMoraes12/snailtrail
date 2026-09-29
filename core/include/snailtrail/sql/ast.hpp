#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/sql/statement_kind.hpp"

namespace snailtrail::sql {

class ExprVisitor;
class StatementVisitor;
class SelectStatement;
class Parser;

class Expr {
public:
    Expr() = default;
    Expr(const Expr&) = delete;
    Expr& operator=(const Expr&) = delete;
    Expr(Expr&&) = delete;
    Expr& operator=(Expr&&) = delete;
    virtual ~Expr() = default;

    virtual void accept(ExprVisitor& visitor) const = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

enum class LiteralKind : std::uint8_t {
    String,
    Number,
    Hex,
    Null,
    Boolean,
    Placeholder,
    Variable,
    Keyword,
};

class Literal final : public Expr {
public:
    Literal(LiteralKind kind, std::string text);

    [[nodiscard]] LiteralKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::string value() const;
    [[nodiscard]] std::optional<double> number() const;
    [[nodiscard]] bool is_constant() const noexcept;

    void accept(ExprVisitor& visitor) const override;

private:
    LiteralKind kind_;
    std::string text_;
};

class ColumnRef final : public Expr {
public:
    ColumnRef(std::string schema, std::string table, std::string name);

    [[nodiscard]] const std::string& schema() const noexcept { return schema_; }
    [[nodiscard]] const std::string& table() const noexcept { return table_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::string qualified_name() const;

    void accept(ExprVisitor& visitor) const override;

private:
    std::string schema_;
    std::string table_;
    std::string name_;
};

enum class UnaryOp : std::uint8_t { Not, Negate, Plus, BitNot, Binary };

class UnaryExpr final : public Expr {
public:
    UnaryExpr(UnaryOp op, ExprPtr operand);

    [[nodiscard]] UnaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }

    void accept(ExprVisitor& visitor) const override;

private:
    UnaryOp op_;
    ExprPtr operand_;
};

enum class BinaryOp : std::uint8_t {
    Or,
    Xor,
    And,
    Eq,
    NullSafeEq,
    NotEq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Like,
    NotLike,
    Regexp,
    NotRegexp,
    BitOr,
    BitAnd,
    ShiftLeft,
    ShiftRight,
    Add,
    Sub,
    Mul,
    Div,
    IntDiv,
    Mod,
    BitXor,
    JsonExtract,
    JsonUnquoteExtract,
    Assign,
};

std::string_view binary_op_symbol(BinaryOp op) noexcept;
bool is_comparison(BinaryOp op) noexcept;
bool is_logical(BinaryOp op) noexcept;
bool is_arithmetic(BinaryOp op) noexcept;
int precedence(BinaryOp op) noexcept;

class BinaryExpr final : public Expr {
public:
    BinaryExpr(BinaryOp op, ExprPtr left, ExprPtr right);

    [[nodiscard]] BinaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expr& left() const noexcept { return *left_; }
    [[nodiscard]] const Expr& right() const noexcept { return *right_; }

    void accept(ExprVisitor& visitor) const override;

private:
    BinaryOp op_;
    ExprPtr left_;
    ExprPtr right_;
};

class InExpr final : public Expr {
public:
    InExpr(ExprPtr operand, std::vector<ExprPtr> values, bool negated);
    InExpr(ExprPtr operand, std::unique_ptr<SelectStatement> subquery, bool negated);
    ~InExpr() override;

    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }
    [[nodiscard]] const std::vector<ExprPtr>& values() const noexcept { return values_; }
    [[nodiscard]] const SelectStatement* subquery() const noexcept { return subquery_.get(); }
    [[nodiscard]] bool negated() const noexcept { return negated_; }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr operand_;
    std::vector<ExprPtr> values_;
    std::unique_ptr<SelectStatement> subquery_;
    bool negated_;
};

class BetweenExpr final : public Expr {
public:
    BetweenExpr(ExprPtr operand, ExprPtr low, ExprPtr high, bool negated);

    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }
    [[nodiscard]] const Expr& low() const noexcept { return *low_; }
    [[nodiscard]] const Expr& high() const noexcept { return *high_; }
    [[nodiscard]] bool negated() const noexcept { return negated_; }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr operand_;
    ExprPtr low_;
    ExprPtr high_;
    bool negated_;
};

enum class IsTest : std::uint8_t { Null, True, False, Unknown };

class IsExpr final : public Expr {
public:
    IsExpr(ExprPtr operand, IsTest test, bool negated);

    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }
    [[nodiscard]] IsTest test() const noexcept { return test_; }
    [[nodiscard]] bool negated() const noexcept { return negated_; }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr operand_;
    IsTest test_;
    bool negated_;
};

class FunctionCall final : public Expr {
public:
    FunctionCall(std::string name, std::vector<ExprPtr> args, bool star = false,
                 bool distinct = false, bool windowed = false);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<ExprPtr>& args() const noexcept { return args_; }
    [[nodiscard]] bool star() const noexcept { return star_; }
    [[nodiscard]] bool distinct() const noexcept { return distinct_; }
    [[nodiscard]] bool windowed() const noexcept { return windowed_; }
    [[nodiscard]] bool is_aggregate() const noexcept;

    void accept(ExprVisitor& visitor) const override;

private:
    std::string name_;
    std::vector<ExprPtr> args_;
    bool star_;
    bool distinct_;
    bool windowed_;
};

struct WhenClause {
    ExprPtr condition;
    ExprPtr result;
};

class CaseExpr final : public Expr {
public:
    CaseExpr(ExprPtr operand, std::vector<WhenClause> branches, ExprPtr else_result);

    [[nodiscard]] const Expr* operand() const noexcept { return operand_.get(); }
    [[nodiscard]] const std::vector<WhenClause>& branches() const noexcept { return branches_; }
    [[nodiscard]] const Expr* else_result() const noexcept { return else_result_.get(); }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr operand_;
    std::vector<WhenClause> branches_;
    ExprPtr else_result_;
};

class CastExpr final : public Expr {
public:
    CastExpr(ExprPtr operand, std::string target_type);

    [[nodiscard]] const Expr& operand() const noexcept { return *operand_; }
    [[nodiscard]] const std::string& target_type() const noexcept { return target_type_; }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr operand_;
    std::string target_type_;
};

class IntervalExpr final : public Expr {
public:
    IntervalExpr(ExprPtr value, std::string unit);

    [[nodiscard]] const Expr& value() const noexcept { return *value_; }
    [[nodiscard]] const std::string& unit() const noexcept { return unit_; }

    void accept(ExprVisitor& visitor) const override;

private:
    ExprPtr value_;
    std::string unit_;
};

enum class Quantifier : std::uint8_t { None, Exists, Any, All };

class SubqueryExpr final : public Expr {
public:
    SubqueryExpr(std::unique_ptr<SelectStatement> query, Quantifier quantifier);
    ~SubqueryExpr() override;

    [[nodiscard]] const SelectStatement& query() const noexcept { return *query_; }
    [[nodiscard]] Quantifier quantifier() const noexcept { return quantifier_; }

    void accept(ExprVisitor& visitor) const override;

private:
    std::unique_ptr<SelectStatement> query_;
    Quantifier quantifier_;
};

class RowExpr final : public Expr {
public:
    explicit RowExpr(std::vector<ExprPtr> items);

    [[nodiscard]] const std::vector<ExprPtr>& items() const noexcept { return items_; }

    void accept(ExprVisitor& visitor) const override;

private:
    std::vector<ExprPtr> items_;
};

class ExprVisitor {
public:
    ExprVisitor() = default;
    ExprVisitor(const ExprVisitor&) = default;
    ExprVisitor& operator=(const ExprVisitor&) = default;
    virtual ~ExprVisitor() = default;

    virtual void visit(const Literal& e) = 0;
    virtual void visit(const ColumnRef& e) = 0;
    virtual void visit(const UnaryExpr& e) = 0;
    virtual void visit(const BinaryExpr& e) = 0;
    virtual void visit(const InExpr& e) = 0;
    virtual void visit(const BetweenExpr& e) = 0;
    virtual void visit(const IsExpr& e) = 0;
    virtual void visit(const FunctionCall& e) = 0;
    virtual void visit(const CaseExpr& e) = 0;
    virtual void visit(const CastExpr& e) = 0;
    virtual void visit(const IntervalExpr& e) = 0;
    virtual void visit(const SubqueryExpr& e) = 0;
    virtual void visit(const RowExpr& e) = 0;
};

class RecursiveExprVisitor : public ExprVisitor {
public:
    void visit(const Literal& e) override;
    void visit(const ColumnRef& e) override;
    void visit(const UnaryExpr& e) override;
    void visit(const BinaryExpr& e) override;
    void visit(const InExpr& e) override;
    void visit(const BetweenExpr& e) override;
    void visit(const IsExpr& e) override;
    void visit(const FunctionCall& e) override;
    void visit(const CaseExpr& e) override;
    void visit(const CastExpr& e) override;
    void visit(const IntervalExpr& e) override;
    void visit(const SubqueryExpr& e) override;
    void visit(const RowExpr& e) override;
};

struct TableRef {
    std::string schema;
    std::string name;
    std::string alias;
    std::unique_ptr<SelectStatement> derived;

    TableRef();
    TableRef(TableRef&&) noexcept;
    TableRef& operator=(TableRef&&) noexcept;
    ~TableRef();

    [[nodiscard]] bool is_derived() const noexcept { return derived != nullptr; }
    [[nodiscard]] const std::string& reference_name() const noexcept {
        return alias.empty() ? name : alias;
    }
};

enum class JoinKind : std::uint8_t { Inner, Left, Right, Cross, Straight };

std::string_view join_kind_name(JoinKind kind) noexcept;

struct Join {
    JoinKind kind = JoinKind::Inner;
    bool natural = false;
    TableRef table;
    ExprPtr condition;
    std::vector<std::string> using_columns;
};

struct SelectItem {
    ExprPtr expr;
    std::string alias;
    bool star = false;
    std::string star_qualifier;
};

struct OrderItem {
    ExprPtr expr;
    bool descending = false;
};

struct Limit {
    ExprPtr count;
    ExprPtr offset;
};

struct Assignment {
    std::unique_ptr<ColumnRef> column;
    ExprPtr value;
};

struct CommonTableExpr {
    std::string name;
    std::vector<std::string> columns;
    std::unique_ptr<SelectStatement> query;

    CommonTableExpr();
    CommonTableExpr(CommonTableExpr&&) noexcept;
    CommonTableExpr& operator=(CommonTableExpr&&) noexcept;
    ~CommonTableExpr();
};

enum class LockMode : std::uint8_t { None, ForUpdate, ForShare };

enum class SetOperator : std::uint8_t { Union, UnionAll, Except, Intersect };

std::string_view set_operator_name(SetOperator op) noexcept;

class Statement {
public:
    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;
    virtual ~Statement() = default;

    [[nodiscard]] virtual StatementKind kind() const noexcept = 0;
    virtual void accept(StatementVisitor& visitor) const = 0;
};

using StatementPtr = std::unique_ptr<Statement>;

struct SetBranch {
    SetOperator op = SetOperator::Union;
    std::unique_ptr<SelectStatement> select;

    SetBranch();
    SetBranch(SetBranch&&) noexcept;
    SetBranch& operator=(SetBranch&&) noexcept;
    ~SetBranch();
};

class SelectStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override { return StatementKind::Select; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const std::vector<CommonTableExpr>& ctes() const noexcept { return ctes_; }
    [[nodiscard]] bool distinct() const noexcept { return distinct_; }
    [[nodiscard]] const std::vector<SelectItem>& items() const noexcept { return items_; }
    [[nodiscard]] const std::vector<TableRef>& from() const noexcept { return from_; }
    [[nodiscard]] const std::vector<Join>& joins() const noexcept { return joins_; }
    [[nodiscard]] const Expr* where() const noexcept { return where_.get(); }
    [[nodiscard]] const std::vector<ExprPtr>& group_by() const noexcept { return group_by_; }
    [[nodiscard]] bool with_rollup() const noexcept { return with_rollup_; }
    [[nodiscard]] const Expr* having() const noexcept { return having_.get(); }
    [[nodiscard]] const std::vector<OrderItem>& order_by() const noexcept { return order_by_; }
    [[nodiscard]] const std::optional<Limit>& limit() const noexcept { return limit_; }
    [[nodiscard]] LockMode lock_mode() const noexcept { return lock_mode_; }
    [[nodiscard]] const std::vector<SetBranch>& set_operations() const noexcept {
        return set_operations_;
    }

private:
    friend class Parser;

    std::vector<CommonTableExpr> ctes_;
    bool distinct_ = false;
    std::vector<SelectItem> items_;
    std::vector<TableRef> from_;
    std::vector<Join> joins_;
    ExprPtr where_;
    std::vector<ExprPtr> group_by_;
    bool with_rollup_ = false;
    ExprPtr having_;
    std::vector<OrderItem> order_by_;
    std::optional<Limit> limit_;
    LockMode lock_mode_ = LockMode::None;
    std::vector<SetBranch> set_operations_;
};

class InsertStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override {
        return replace_ ? StatementKind::Replace : StatementKind::Insert;
    }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] bool replace() const noexcept { return replace_; }
    [[nodiscard]] bool ignore() const noexcept { return ignore_; }
    [[nodiscard]] const TableRef& table() const noexcept { return table_; }
    [[nodiscard]] const std::vector<std::string>& columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t row_count() const noexcept { return row_count_; }
    [[nodiscard]] const SelectStatement* select() const noexcept { return select_.get(); }
    [[nodiscard]] const std::vector<Assignment>& assignments() const noexcept {
        return assignments_;
    }
    [[nodiscard]] const std::vector<Assignment>& on_duplicate_key_update() const noexcept {
        return on_duplicate_;
    }

private:
    friend class Parser;

    bool replace_ = false;
    bool ignore_ = false;
    TableRef table_;
    std::vector<std::string> columns_;
    std::size_t row_count_ = 0;
    std::unique_ptr<SelectStatement> select_;
    std::vector<Assignment> assignments_;
    std::vector<Assignment> on_duplicate_;
};

class UpdateStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override { return StatementKind::Update; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const std::vector<TableRef>& tables() const noexcept { return tables_; }
    [[nodiscard]] const std::vector<Join>& joins() const noexcept { return joins_; }
    [[nodiscard]] const std::vector<Assignment>& assignments() const noexcept {
        return assignments_;
    }
    [[nodiscard]] const Expr* where() const noexcept { return where_.get(); }
    [[nodiscard]] const std::vector<OrderItem>& order_by() const noexcept { return order_by_; }
    [[nodiscard]] const std::optional<Limit>& limit() const noexcept { return limit_; }

private:
    friend class Parser;

    std::vector<TableRef> tables_;
    std::vector<Join> joins_;
    std::vector<Assignment> assignments_;
    ExprPtr where_;
    std::vector<OrderItem> order_by_;
    std::optional<Limit> limit_;
};

class DeleteStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override { return StatementKind::Delete; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const std::vector<std::string>& targets() const noexcept { return targets_; }
    [[nodiscard]] const std::vector<TableRef>& from() const noexcept { return from_; }
    [[nodiscard]] const std::vector<Join>& joins() const noexcept { return joins_; }
    [[nodiscard]] const Expr* where() const noexcept { return where_.get(); }
    [[nodiscard]] const std::vector<OrderItem>& order_by() const noexcept { return order_by_; }
    [[nodiscard]] const std::optional<Limit>& limit() const noexcept { return limit_; }

private:
    friend class Parser;

    std::vector<std::string> targets_;
    std::vector<TableRef> from_;
    std::vector<Join> joins_;
    ExprPtr where_;
    std::vector<OrderItem> order_by_;
    std::optional<Limit> limit_;
};

enum class IndexKind : std::uint8_t { Primary, Unique, Regular, Fulltext, Spatial };

std::string_view index_kind_name(IndexKind kind) noexcept;

struct IndexDefinition {
    std::string name;
    IndexKind kind = IndexKind::Regular;
    std::vector<std::string> columns;
};

struct ColumnDefinition {
    std::string name;
    std::string type;
    bool nullable = true;
    bool auto_increment = false;
};

class CreateTableStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override { return StatementKind::Ddl; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const TableRef& table() const noexcept { return table_; }
    [[nodiscard]] const std::vector<ColumnDefinition>& columns() const noexcept {
        return columns_;
    }
    [[nodiscard]] const std::vector<IndexDefinition>& indexes() const noexcept {
        return indexes_;
    }
    [[nodiscard]] bool temporary() const noexcept { return temporary_; }

private:
    friend class Parser;

    TableRef table_;
    std::vector<ColumnDefinition> columns_;
    std::vector<IndexDefinition> indexes_;
    bool temporary_ = false;
};

class AlterTableStatement final : public Statement {
public:
    [[nodiscard]] StatementKind kind() const noexcept override { return StatementKind::Ddl; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const TableRef& table() const noexcept { return table_; }
    [[nodiscard]] const std::vector<IndexDefinition>& added_indexes() const noexcept {
        return added_indexes_;
    }
    [[nodiscard]] const std::vector<std::string>& dropped_indexes() const noexcept {
        return dropped_indexes_;
    }
    [[nodiscard]] const std::vector<ColumnDefinition>& added_columns() const noexcept {
        return added_columns_;
    }

private:
    friend class Parser;

    TableRef table_;
    std::vector<IndexDefinition> added_indexes_;
    std::vector<std::string> dropped_indexes_;
    std::vector<ColumnDefinition> added_columns_;
};

class OtherStatement final : public Statement {
public:
    OtherStatement(StatementKind kind, std::string keyword);

    [[nodiscard]] StatementKind kind() const noexcept override { return kind_; }
    void accept(StatementVisitor& visitor) const override;

    [[nodiscard]] const std::string& keyword() const noexcept { return keyword_; }

private:
    StatementKind kind_;
    std::string keyword_;
};

class StatementVisitor {
public:
    StatementVisitor() = default;
    StatementVisitor(const StatementVisitor&) = default;
    StatementVisitor& operator=(const StatementVisitor&) = default;
    virtual ~StatementVisitor() = default;

    virtual void visit(const SelectStatement& s) = 0;
    virtual void visit(const InsertStatement& s) = 0;
    virtual void visit(const UpdateStatement& s) = 0;
    virtual void visit(const DeleteStatement& s) = 0;
    virtual void visit(const CreateTableStatement& s) = 0;
    virtual void visit(const AlterTableStatement& s) = 0;
    virtual void visit(const OtherStatement& s) = 0;
};

}
