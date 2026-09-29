#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/sql/ast.hpp"
#include "snailtrail/sql/token.hpp"

namespace snailtrail::sql {

class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, std::size_t offset);

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

class Parser {
public:
    static constexpr int max_depth = 300;

    explicit Parser(std::string_view sql);

    StatementPtr parse_statement();
    std::vector<StatementPtr> parse_script(std::vector<ParseError>* errors = nullptr);
    ExprPtr parse_expression();

private:
    class DepthGuard;

    [[nodiscard]] const Token& peek(std::size_t ahead = 0) const noexcept;
    const Token& advance() noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool peek_word(std::string_view word, std::size_t ahead = 0) const noexcept;
    [[nodiscard]] bool peek_kind(TokenKind kind, std::size_t ahead = 0) const noexcept;
    bool accept_word(std::string_view word) noexcept;
    bool accept(TokenKind kind) noexcept;
    bool accept_op(std::string_view op) noexcept;
    void expect_word(std::string_view word);
    const Token& expect(TokenKind kind, std::string_view what);
    [[noreturn]] void fail(const std::string& message) const;
    [[noreturn]] void fail_here(std::string_view expected) const;

    StatementPtr statement();
    std::vector<CommonTableExpr> common_table_exprs();
    std::unique_ptr<SelectStatement> select_statement();
    std::unique_ptr<SelectStatement> select_operand();
    std::unique_ptr<SelectStatement> select_core();
    void select_items(SelectStatement& s);
    void table_references(std::vector<TableRef>& from, std::vector<Join>& joins);
    TableRef table_factor(std::vector<Join>& joins);
    bool try_join(std::vector<Join>& joins);
    TableRef table_name();
    std::string identifier(std::string_view what);
    std::string optional_alias();
    void skip_index_hints();
    void skip_partition_clause();
    std::vector<OrderItem> order_by_list();
    std::optional<Limit> limit_clause();
    void locking_clause(SelectStatement& s);
    std::vector<Assignment> assignments();
    std::unique_ptr<ColumnRef> column_reference();
    std::vector<std::string> identifier_list();

    StatementPtr insert_statement();
    StatementPtr update_statement();
    StatementPtr delete_statement();
    TableRef delete_target();
    StatementPtr create_statement();
    StatementPtr alter_statement();
    StatementPtr other_statement();
    void create_definitions(CreateTableStatement& s);
    ColumnDefinition column_definition(std::vector<IndexDefinition>& inline_indexes);
    IndexDefinition index_definition();
    std::vector<std::string> key_parts();
    std::string data_type();

    ExprPtr expression();
    ExprPtr or_expr();
    ExprPtr xor_expr();
    ExprPtr and_expr();
    ExprPtr not_expr();
    ExprPtr predicate();
    ExprPtr in_tail(ExprPtr operand, bool negated);
    ExprPtr bit_or();
    ExprPtr bit_and();
    ExprPtr shift();
    ExprPtr additive();
    ExprPtr multiplicative();
    ExprPtr bit_xor();
    ExprPtr unary();
    ExprPtr postfix(ExprPtr e);
    ExprPtr primary();
    ExprPtr word_primary();
    ExprPtr function_call(std::string name);
    ExprPtr case_expr();
    ExprPtr cast_expr();
    ExprPtr convert_expr();
    ExprPtr match_expr();
    std::vector<ExprPtr> expression_list();
    std::string collect_until_close();
    void skip_balanced();
    void skip_to_statement_end();
    [[nodiscard]] bool starts_select(std::size_t ahead = 0) const noexcept;

    std::string_view source_;
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    int depth_ = 0;
};

StatementPtr parse(std::string_view sql);
StatementPtr try_parse(std::string_view sql, std::string* error = nullptr);

}
