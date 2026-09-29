#include "snailtrail/sql/parser.hpp"

#include <format>
#include <span>

#include "snailtrail/sql/keywords.hpp"
#include "snailtrail/sql/lexer.hpp"
#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::sql {

ParseError::ParseError(const std::string& message, std::size_t offset)
    : std::runtime_error(message), offset_(offset) {}

namespace {

bool is_niladic_function(std::string_view w) noexcept {
    static constexpr std::string_view names[] = {
        "current_date", "current_time", "current_timestamp", "current_user", "localtime",
        "localtimestamp", "utc_date", "utc_time", "utc_timestamp"};
    for (auto n : names) {
        if (util::iequals(w, n)) return true;
    }
    return false;
}

bool usable_as_bare_identifier(std::string_view w) noexcept {
    return util::iequals(w, "end") || util::iequals(w, "offset") || util::iequals(w, "full");
}

bool blocks_implicit_alias(std::string_view w) noexcept {
    static constexpr std::string_view words[] = {"with", "rollup", "lateral", "nowait", "skip",
                                                 "escape", "sounds", "member", "collate", "over",
                                                 "returning", "row", "dual"};
    if (is_reserved_word(w)) return true;
    for (auto x : words) {
        if (util::iequals(w, x)) return true;
    }
    return false;
}

bool comparison_op(const Token& t, BinaryOp& op) noexcept {
    if (t.kind != TokenKind::Operator) return false;
    if (t.text == "=") {
        op = BinaryOp::Eq;
    } else if (t.text == "<=>") {
        op = BinaryOp::NullSafeEq;
    } else if (t.text == "<>" || t.text == "!=") {
        op = BinaryOp::NotEq;
    } else if (t.text == "<") {
        op = BinaryOp::Less;
    } else if (t.text == "<=") {
        op = BinaryOp::LessEq;
    } else if (t.text == ">") {
        op = BinaryOp::Greater;
    } else if (t.text == ">=") {
        op = BinaryOp::GreaterEq;
    } else {
        return false;
    }
    return true;
}

bool word_like(TokenKind k) noexcept {
    return k == TokenKind::Word || k == TokenKind::Number || k == TokenKind::String ||
           k == TokenKind::QuotedIdentifier || k == TokenKind::HexNumber;
}

template <typename T, typename... Args>
ExprPtr make(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

}

class Parser::DepthGuard {
public:
    explicit DepthGuard(Parser& parser) : parser_(parser) {
        if (parser_.depth_ >= max_depth) parser_.fail("statement is nested too deeply");
        ++parser_.depth_;
    }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    ~DepthGuard() { --parser_.depth_; }

private:
    Parser& parser_;
};

Parser::Parser(std::string_view sql) : source_(sql), tokens_(Lexer::tokenize(sql)) {}

const Token& Parser::peek(std::size_t ahead) const noexcept {
    const std::size_t at = pos_ + ahead;
    return at < tokens_.size() ? tokens_[at] : tokens_.back();
}

const Token& Parser::advance() noexcept {
    const Token& t = peek();
    if (pos_ + 1 < tokens_.size()) ++pos_;
    return t;
}

bool Parser::at_end() const noexcept { return peek().kind == TokenKind::End; }

bool Parser::peek_word(std::string_view word, std::size_t ahead) const noexcept {
    return peek(ahead).is_word(word);
}

bool Parser::peek_kind(TokenKind kind, std::size_t ahead) const noexcept {
    return peek(ahead).kind == kind;
}

bool Parser::accept_word(std::string_view word) noexcept {
    if (!peek_word(word)) return false;
    advance();
    return true;
}

bool Parser::accept(TokenKind kind) noexcept {
    if (!peek_kind(kind)) return false;
    advance();
    return true;
}

bool Parser::accept_op(std::string_view op) noexcept {
    if (!peek().is_op(op)) return false;
    advance();
    return true;
}

void Parser::expect_word(std::string_view word) {
    if (!accept_word(word)) fail_here(util::to_upper(word));
}

const Token& Parser::expect(TokenKind kind, std::string_view what) {
    if (!peek_kind(kind)) fail_here(what);
    return advance();
}

void Parser::fail(const std::string& message) const { throw ParseError(message, peek().offset); }

void Parser::fail_here(std::string_view expected) const {
    const Token& t = peek();
    const std::string found =
        t.kind == TokenKind::End ? std::string("end of statement") : "'" + std::string(t.text) + "'";
    fail(std::format("expected {} but found {} at offset {}", expected, found, t.offset));
}

bool Parser::starts_select(std::size_t ahead) const noexcept {
    std::size_t i = ahead;
    while (peek_kind(TokenKind::LParen, i)) ++i;
    return peek_word("select", i) || peek_word("with", i);
}

void Parser::skip_balanced() {
    if (!peek_kind(TokenKind::LParen)) return;
    int depth = 0;
    while (!at_end()) {
        const Token& t = advance();
        if (t.kind == TokenKind::LParen) {
            ++depth;
        } else if (t.kind == TokenKind::RParen && --depth == 0) {
            return;
        }
    }
}

void Parser::skip_to_statement_end() {
    while (!at_end() && !peek_kind(TokenKind::Semicolon)) {
        if (peek_kind(TokenKind::LParen)) {
            skip_balanced();
        } else {
            advance();
        }
    }
}

std::string Parser::collect_until_close() {
    std::string out;
    int depth = 0;
    TokenKind previous = TokenKind::LParen;
    while (!at_end()) {
        const Token& t = peek();
        if (t.kind == TokenKind::RParen && depth == 0) break;
        if (t.kind == TokenKind::LParen) ++depth;
        if (t.kind == TokenKind::RParen) --depth;
        if (word_like(t.kind) && word_like(previous)) out += ' ';
        out += t.kind == TokenKind::Word ? util::to_lower(t.text) : std::string(t.text);
        previous = t.kind;
        advance();
    }
    return out;
}

StatementPtr Parser::parse_statement() {
    while (accept(TokenKind::Semicolon)) {
    }
    if (at_end()) fail("empty statement");
    auto s = statement();
    while (accept(TokenKind::Semicolon)) {
    }
    if (!at_end()) fail_here("end of statement");
    return s;
}

std::vector<StatementPtr> Parser::parse_script(std::vector<ParseError>* errors) {
    std::vector<StatementPtr> out;
    while (true) {
        while (accept(TokenKind::Semicolon)) {
        }
        if (at_end()) break;
        try {
            depth_ = 0;
            auto s = statement();
            if (!at_end() && !peek_kind(TokenKind::Semicolon)) fail_here("';'");
            out.push_back(std::move(s));
        } catch (const ParseError& e) {
            if (errors != nullptr) errors->push_back(e);
            skip_to_statement_end();
        }
    }
    return out;
}

ExprPtr Parser::parse_expression() {
    auto e = expression();
    if (!at_end()) fail_here("end of expression");
    return e;
}

StatementPtr Parser::statement() {
    const Token& t = peek();
    if (t.kind == TokenKind::LParen) return select_statement();
    if (t.kind != TokenKind::Word) fail_here("a statement");

    if (peek_word("select")) return select_statement();
    if (peek_word("with")) {
        const auto rest = std::span<const Token>(tokens_).subspan(pos_);
        const StatementKind k = classify_statement(rest);
        if (k == StatementKind::Update || k == StatementKind::Delete) {
            common_table_exprs();
            return k == StatementKind::Update ? update_statement() : delete_statement();
        }
        return select_statement();
    }
    if (peek_word("insert") || peek_word("replace")) return insert_statement();
    if (peek_word("update")) return update_statement();
    if (peek_word("delete")) return delete_statement();
    if (peek_word("create")) return create_statement();
    if (peek_word("alter")) return alter_statement();
    return other_statement();
}

std::vector<CommonTableExpr> Parser::common_table_exprs() {
    expect_word("with");
    accept_word("recursive");
    std::vector<CommonTableExpr> out;
    do {
        CommonTableExpr cte;
        cte.name = identifier("a CTE name");
        if (accept(TokenKind::LParen)) {
            cte.columns = identifier_list();
            expect(TokenKind::RParen, "')'");
        }
        expect_word("as");
        expect(TokenKind::LParen, "'('");
        cte.query = select_statement();
        expect(TokenKind::RParen, "')'");
        out.push_back(std::move(cte));
    } while (accept(TokenKind::Comma));
    return out;
}

std::unique_ptr<SelectStatement> Parser::select_statement() {
    DepthGuard guard(*this);
    std::vector<CommonTableExpr> ctes;
    if (peek_word("with")) ctes = common_table_exprs();

    auto first = select_operand();
    while (true) {
        SetOperator op{};
        if (accept_word("union")) {
            op = accept_word("all") ? SetOperator::UnionAll : SetOperator::Union;
            accept_word("distinct");
        } else if (accept_word("except")) {
            op = SetOperator::Except;
            if (!accept_word("all")) accept_word("distinct");
        } else if (accept_word("intersect")) {
            op = SetOperator::Intersect;
            if (!accept_word("all")) accept_word("distinct");
        } else {
            break;
        }
        SetBranch branch;
        branch.op = op;
        branch.select = select_operand();
        first->set_operations_.push_back(std::move(branch));
    }
    if (first->order_by_.empty() && peek_word("order") && peek_word("by", 1)) {
        advance();
        advance();
        first->order_by_ = order_by_list();
    }
    if (!first->limit_ && peek_word("limit")) first->limit_ = limit_clause();

    for (auto& cte : ctes) first->ctes_.push_back(std::move(cte));
    return first;
}

std::unique_ptr<SelectStatement> Parser::select_operand() {
    if (peek_kind(TokenKind::LParen) && starts_select(1)) {
        advance();
        auto s = select_statement();
        expect(TokenKind::RParen, "')'");
        return s;
    }
    return select_core();
}

std::unique_ptr<SelectStatement> Parser::select_core() {
    expect_word("select");
    auto s = std::make_unique<SelectStatement>();

    static constexpr std::string_view options[] = {
        "all",           "high_priority",   "straight_join", "sql_small_result",
        "sql_big_result", "sql_buffer_result", "sql_no_cache", "sql_cache",
        "sql_calc_found_rows"};
    while (true) {
        if (accept_word("distinct") || accept_word("distinctrow")) {
            s->distinct_ = true;
            continue;
        }
        bool matched = false;
        for (auto o : options) {
            if (accept_word(o)) {
                matched = true;
                break;
            }
        }
        if (!matched) break;
    }

    select_items(*s);

    auto skip_into = [this] {
        static constexpr std::string_view stops[] = {"from", "where", "group", "having", "order",
                                                     "limit", "for", "lock", "union", "window"};
        while (!at_end() && !peek_kind(TokenKind::RParen) && !peek_kind(TokenKind::Semicolon)) {
            bool stop = false;
            for (auto w : stops) stop = stop || peek_word(w);
            if (stop) break;
            advance();
        }
    };

    if (accept_word("into")) skip_into();
    if (accept_word("from")) {
        if (!accept_word("dual")) table_references(s->from_, s->joins_);
    }
    if (accept_word("where")) s->where_ = expression();
    if (peek_word("group") && peek_word("by", 1)) {
        advance();
        advance();
        do {
            s->group_by_.push_back(expression());
            if (!accept_word("asc")) accept_word("desc");
        } while (accept(TokenKind::Comma));
        if (peek_word("with") && peek_word("rollup", 1)) {
            advance();
            advance();
            s->with_rollup_ = true;
        }
    }
    if (accept_word("having")) s->having_ = expression();
    if (accept_word("window")) {
        do {
            identifier("a window name");
            expect_word("as");
            if (!peek_kind(TokenKind::LParen)) fail_here("'('");
            skip_balanced();
        } while (accept(TokenKind::Comma));
    }
    if (peek_word("order") && peek_word("by", 1)) {
        advance();
        advance();
        s->order_by_ = order_by_list();
    }
    if (peek_word("limit")) s->limit_ = limit_clause();
    locking_clause(*s);
    if (accept_word("into")) skip_into();
    locking_clause(*s);
    return s;
}

void Parser::select_items(SelectStatement& s) {
    do {
        SelectItem item;
        if (accept_op("*")) {
            item.star = true;
        } else if (peek().is_identifier() && peek_kind(TokenKind::Dot, 1) && peek(2).is_op("*")) {
            item.star = true;
            item.star_qualifier = unquote_identifier(advance());
            advance();
            advance();
        } else if (peek().is_identifier() && peek_kind(TokenKind::Dot, 1) &&
                   peek(2).is_identifier() && peek_kind(TokenKind::Dot, 3) && peek(4).is_op("*")) {
            item.star = true;
            item.star_qualifier = unquote_identifier(advance());
            advance();
            item.star_qualifier += "." + unquote_identifier(advance());
            advance();
            advance();
        } else {
            item.expr = expression();
            item.alias = optional_alias();
        }
        s.items_.push_back(std::move(item));
    } while (accept(TokenKind::Comma));
}

std::string Parser::identifier(std::string_view what) {
    const Token& t = peek();
    if (t.kind == TokenKind::QuotedIdentifier) return unquote_identifier(advance());
    if (t.kind == TokenKind::Word && (!is_reserved_word(t.text) || usable_as_bare_identifier(t.text))) {
        return std::string(advance().text);
    }
    fail_here(what);
}

std::string Parser::optional_alias() {
    if (accept_word("as")) {
        const Token& t = peek();
        if (t.is_identifier()) return unquote_identifier(advance());
        if (t.kind == TokenKind::String) return unquote_string(advance().text);
        fail_here("an alias");
    }
    const Token& t = peek();
    if (t.kind == TokenKind::QuotedIdentifier) return unquote_identifier(advance());
    if (t.kind == TokenKind::Word && !blocks_implicit_alias(t.text)) {
        return std::string(advance().text);
    }
    return {};
}

std::vector<std::string> Parser::identifier_list() {
    std::vector<std::string> out;
    do {
        out.push_back(identifier("a column name"));
    } while (accept(TokenKind::Comma));
    return out;
}

TableRef Parser::table_name() {
    TableRef t;
    std::string first = identifier("a table name");
    if (accept(TokenKind::Dot)) {
        t.schema = std::move(first);
        t.name = identifier("a table name");
    } else {
        t.name = std::move(first);
    }
    return t;
}

void Parser::skip_partition_clause() {
    if (peek_word("partition") && peek_kind(TokenKind::LParen, 1)) {
        advance();
        skip_balanced();
    }
}

void Parser::skip_index_hints() {
    while ((peek_word("use") || peek_word("ignore") || peek_word("force")) &&
           (peek_word("index", 1) || peek_word("key", 1))) {
        advance();
        advance();
        if (accept_word("for")) {
            if (!accept_word("join")) {
                advance();
                expect_word("by");
            }
        }
        if (!peek_kind(TokenKind::LParen)) fail_here("'('");
        skip_balanced();
        accept(TokenKind::Comma);
    }
}

void Parser::table_references(std::vector<TableRef>& from, std::vector<Join>& joins) {
    from.push_back(table_factor(joins));
    while (true) {
        if (accept(TokenKind::Comma)) {
            from.push_back(table_factor(joins));
        } else if (!try_join(joins)) {
            break;
        }
    }
}

TableRef Parser::table_factor(std::vector<Join>& joins) {
    accept_word("lateral");
    if (peek_kind(TokenKind::LParen)) {
        if (starts_select(1)) {
            advance();
            TableRef t;
            t.derived = select_statement();
            expect(TokenKind::RParen, "')'");
            t.alias = optional_alias();
            if (peek_kind(TokenKind::LParen)) skip_balanced();
            return t;
        }
        advance();
        std::vector<TableRef> inner;
        table_references(inner, joins);
        expect(TokenKind::RParen, "')'");
        TableRef first = std::move(inner.front());
        for (std::size_t i = 1; i < inner.size(); ++i) {
            Join j;
            j.kind = JoinKind::Cross;
            j.table = std::move(inner[i]);
            joins.push_back(std::move(j));
        }
        return first;
    }
    TableRef t = table_name();
    skip_partition_clause();
    t.alias = optional_alias();
    skip_index_hints();
    return t;
}

bool Parser::try_join(std::vector<Join>& joins) {
    const std::size_t saved = pos_;
    Join j;
    if (accept_word("straight_join")) {
        j.kind = JoinKind::Straight;
    } else {
        j.natural = accept_word("natural");
        if (accept_word("inner")) {
            j.kind = JoinKind::Inner;
        } else if (accept_word("cross")) {
            j.kind = JoinKind::Cross;
        } else if (accept_word("left")) {
            accept_word("outer");
            j.kind = JoinKind::Left;
        } else if (accept_word("right")) {
            accept_word("outer");
            j.kind = JoinKind::Right;
        }
        if (!accept_word("join")) {
            pos_ = saved;
            return false;
        }
    }
    j.table = table_factor(joins);
    if (accept_word("on")) {
        j.condition = expression();
    } else if (accept_word("using")) {
        expect(TokenKind::LParen, "'('");
        j.using_columns = identifier_list();
        expect(TokenKind::RParen, "')'");
    }
    joins.push_back(std::move(j));
    return true;
}

std::vector<OrderItem> Parser::order_by_list() {
    std::vector<OrderItem> out;
    do {
        OrderItem item;
        item.expr = expression();
        if (accept_word("desc")) {
            item.descending = true;
        } else {
            accept_word("asc");
        }
        out.push_back(std::move(item));
    } while (accept(TokenKind::Comma));
    return out;
}

std::optional<Limit> Parser::limit_clause() {
    expect_word("limit");
    Limit limit;
    ExprPtr first = unary();
    if (accept(TokenKind::Comma)) {
        limit.offset = std::move(first);
        limit.count = unary();
    } else if (accept_word("offset")) {
        limit.count = std::move(first);
        limit.offset = unary();
    } else {
        limit.count = std::move(first);
    }
    return limit;
}

void Parser::locking_clause(SelectStatement& s) {
    while (true) {
        if (peek_word("for") && (peek_word("update", 1) || peek_word("share", 1))) {
            advance();
            s.lock_mode_ = accept_word("update") ? LockMode::ForUpdate : LockMode::ForShare;
            if (s.lock_mode_ == LockMode::ForShare) advance();
            if (accept_word("of")) {
                do {
                    table_name();
                } while (accept(TokenKind::Comma));
            }
            if (!accept_word("nowait") && accept_word("skip")) expect_word("locked");
        } else if (peek_word("lock") && peek_word("in", 1)) {
            advance();
            advance();
            expect_word("share");
            expect_word("mode");
            s.lock_mode_ = LockMode::ForShare;
        } else {
            return;
        }
    }
}

std::unique_ptr<ColumnRef> Parser::column_reference() {
    std::string a = identifier("a column name");
    if (!accept(TokenKind::Dot)) return std::make_unique<ColumnRef>("", "", std::move(a));
    std::string b = identifier("a column name");
    if (!accept(TokenKind::Dot)) return std::make_unique<ColumnRef>("", std::move(a), std::move(b));
    std::string c = identifier("a column name");
    return std::make_unique<ColumnRef>(std::move(a), std::move(b), std::move(c));
}

std::vector<Assignment> Parser::assignments() {
    std::vector<Assignment> out;
    do {
        Assignment a;
        a.column = column_reference();
        if (!accept_op("=") && !accept_op(":=")) fail_here("'='");
        a.value = expression();
        out.push_back(std::move(a));
    } while (accept(TokenKind::Comma));
    return out;
}

StatementPtr Parser::insert_statement() {
    auto s = std::make_unique<InsertStatement>();
    s->replace_ = peek_word("replace");
    advance();
    while (accept_word("low_priority") || accept_word("delayed") || accept_word("high_priority")) {
    }
    s->ignore_ = accept_word("ignore");
    accept_word("into");
    s->table_ = table_name();
    skip_partition_clause();

    if (peek_kind(TokenKind::LParen) && !starts_select(1)) {
        advance();
        if (!peek_kind(TokenKind::RParen)) s->columns_ = identifier_list();
        expect(TokenKind::RParen, "')'");
    }

    if (accept_word("values") || accept_word("value")) {
        do {
            accept_word("row");
            if (!peek_kind(TokenKind::LParen)) fail_here("'('");
            skip_balanced();
            ++s->row_count_;
        } while (accept(TokenKind::Comma));
    } else if (accept_word("set")) {
        s->assignments_ = assignments();
        s->row_count_ = 1;
    } else if (starts_select()) {
        s->select_ = select_statement();
    } else if (accept_word("table")) {
        table_name();
    } else {
        fail_here("VALUES, SET or SELECT");
    }

    if (accept_word("as")) {
        identifier("a row alias");
        if (peek_kind(TokenKind::LParen)) skip_balanced();
    }
    if (accept_word("on")) {
        expect_word("duplicate");
        expect_word("key");
        expect_word("update");
        s->on_duplicate_ = assignments();
    }
    return s;
}

StatementPtr Parser::update_statement() {
    expect_word("update");
    while (accept_word("low_priority") || accept_word("ignore")) {
    }
    auto s = std::make_unique<UpdateStatement>();
    table_references(s->tables_, s->joins_);
    expect_word("set");
    s->assignments_ = assignments();
    if (accept_word("where")) s->where_ = expression();
    if (peek_word("order") && peek_word("by", 1)) {
        advance();
        advance();
        s->order_by_ = order_by_list();
    }
    if (peek_word("limit")) s->limit_ = limit_clause();
    return s;
}

TableRef Parser::delete_target() {
    TableRef t;
    t.name = identifier("a table name");
    if (peek_kind(TokenKind::Dot) && peek(1).is_op("*")) {
        advance();
        advance();
        return t;
    }
    if (accept(TokenKind::Dot)) {
        t.schema = std::move(t.name);
        t.name = identifier("a table name");
        if (peek_kind(TokenKind::Dot) && peek(1).is_op("*")) {
            advance();
            advance();
        }
    }
    return t;
}

StatementPtr Parser::delete_statement() {
    expect_word("delete");
    while (accept_word("low_priority") || accept_word("quick") || accept_word("ignore")) {
    }
    auto s = std::make_unique<DeleteStatement>();
    if (accept_word("from")) {
        std::vector<TableRef> tables;
        do {
            TableRef t = delete_target();
            skip_partition_clause();
            t.alias = optional_alias();
            tables.push_back(std::move(t));
        } while (accept(TokenKind::Comma));
        if (accept_word("using")) {
            for (const auto& t : tables) s->targets_.push_back(t.reference_name());
            table_references(s->from_, s->joins_);
        } else {
            s->from_ = std::move(tables);
        }
    } else {
        do {
            s->targets_.push_back(delete_target().name);
        } while (accept(TokenKind::Comma));
        expect_word("from");
        table_references(s->from_, s->joins_);
    }
    if (accept_word("where")) s->where_ = expression();
    if (peek_word("order") && peek_word("by", 1)) {
        advance();
        advance();
        s->order_by_ = order_by_list();
    }
    if (peek_word("limit")) s->limit_ = limit_clause();
    return s;
}

StatementPtr Parser::create_statement() {
    expect_word("create");
    const bool temporary = accept_word("temporary");
    if (accept_word("table")) {
        auto s = std::make_unique<CreateTableStatement>();
        s->temporary_ = temporary;
        if (accept_word("if")) {
            expect_word("not");
            expect_word("exists");
        }
        s->table_ = table_name();
        if (peek_kind(TokenKind::LParen) && !starts_select(1)) {
            advance();
            create_definitions(*s);
            expect(TokenKind::RParen, "')'");
        } else if (accept_word("like")) {
            table_name();
        }
        skip_to_statement_end();
        return s;
    }

    IndexKind kind = IndexKind::Regular;
    if (accept_word("unique")) {
        kind = IndexKind::Unique;
    } else if (accept_word("fulltext")) {
        kind = IndexKind::Fulltext;
    } else if (accept_word("spatial")) {
        kind = IndexKind::Spatial;
    }
    if (accept_word("index")) {
        IndexDefinition index;
        index.kind = kind;
        index.name = identifier("an index name");
        if (accept_word("using")) advance();
        expect_word("on");
        auto s = std::make_unique<AlterTableStatement>();
        s->table_ = table_name();
        index.columns = key_parts();
        s->added_indexes_.push_back(std::move(index));
        skip_to_statement_end();
        return s;
    }
    skip_to_statement_end();
    return std::make_unique<OtherStatement>(StatementKind::Ddl, "create");
}

StatementPtr Parser::alter_statement() {
    expect_word("alter");
    accept_word("online");
    accept_word("ignore");
    if (!accept_word("table")) {
        skip_to_statement_end();
        return std::make_unique<OtherStatement>(StatementKind::Ddl, "alter");
    }
    auto s = std::make_unique<AlterTableStatement>();
    s->table_ = table_name();

    auto skip_definition = [this] {
        while (!at_end() && !peek_kind(TokenKind::Comma) && !peek_kind(TokenKind::Semicolon)) {
            if (peek_kind(TokenKind::LParen)) {
                skip_balanced();
            } else {
                advance();
            }
        }
    };

    do {
        if (accept_word("add")) {
            if (accept_word("constraint") && !peek_word("primary") && !peek_word("unique") &&
                !peek_word("foreign") && !peek_word("check")) {
                identifier("a constraint name");
            }
            if (peek_word("primary") || peek_word("unique") || peek_word("key") ||
                peek_word("index") || peek_word("fulltext") || peek_word("spatial")) {
                s->added_indexes_.push_back(index_definition());
            } else if (peek_word("foreign") || peek_word("check")) {
                skip_definition();
            } else {
                accept_word("column");
                s->added_columns_.push_back(column_definition(s->added_indexes_));
            }
        } else if (accept_word("drop")) {
            if (accept_word("index") || accept_word("key")) {
                s->dropped_indexes_.push_back(identifier("an index name"));
            } else if (accept_word("primary")) {
                expect_word("key");
                s->dropped_indexes_.emplace_back("PRIMARY");
            } else {
                skip_definition();
            }
        } else {
            skip_definition();
        }
    } while (accept(TokenKind::Comma));
    skip_to_statement_end();
    return s;
}

StatementPtr Parser::other_statement() {
    const auto rest = std::span<const Token>(tokens_).subspan(pos_);
    const StatementKind kind = classify_statement(rest);
    std::string keyword = util::to_lower(advance().text);
    skip_to_statement_end();
    return std::make_unique<OtherStatement>(kind, std::move(keyword));
}

void Parser::create_definitions(CreateTableStatement& s) {
    auto skip_definition = [this] {
        while (!at_end() && !peek_kind(TokenKind::Comma) && !peek_kind(TokenKind::RParen)) {
            if (peek_kind(TokenKind::LParen)) {
                skip_balanced();
            } else {
                advance();
            }
        }
    };
    do {
        if (accept_word("constraint") && !peek_word("primary") && !peek_word("unique") &&
            !peek_word("foreign") && !peek_word("check")) {
            identifier("a constraint name");
        }
        if (peek_word("primary") || peek_word("unique") || peek_word("key") ||
            peek_word("index") || peek_word("fulltext") || peek_word("spatial")) {
            s.indexes_.push_back(index_definition());
        } else if (peek_word("foreign") || peek_word("check")) {
            skip_definition();
        } else {
            s.columns_.push_back(column_definition(s.indexes_));
        }
    } while (accept(TokenKind::Comma));
}

IndexDefinition Parser::index_definition() {
    IndexDefinition index;
    if (accept_word("primary")) {
        expect_word("key");
        index.kind = IndexKind::Primary;
        index.name = "PRIMARY";
    } else {
        if (accept_word("unique")) {
            index.kind = IndexKind::Unique;
        } else if (accept_word("fulltext")) {
            index.kind = IndexKind::Fulltext;
        } else if (accept_word("spatial")) {
            index.kind = IndexKind::Spatial;
        }
        if (!accept_word("index")) accept_word("key");
        if (!peek_kind(TokenKind::LParen) && !peek_word("using")) {
            index.name = identifier("an index name");
        }
    }
    if (accept_word("using")) advance();
    index.columns = key_parts();
    while (!at_end() && !peek_kind(TokenKind::Comma) && !peek_kind(TokenKind::RParen) &&
           !peek_kind(TokenKind::Semicolon)) {
        if (peek_kind(TokenKind::LParen)) {
            skip_balanced();
        } else {
            advance();
        }
    }
    return index;
}

std::vector<std::string> Parser::key_parts() {
    expect(TokenKind::LParen, "'('");
    std::vector<std::string> columns;
    do {
        if (accept(TokenKind::LParen)) {
            auto e = expression();
            expect(TokenKind::RParen, "')'");
            columns.push_back("(" + to_sql(*e) + ")");
        } else {
            columns.push_back(identifier("a column name"));
            if (peek_kind(TokenKind::LParen)) skip_balanced();
        }
        if (!accept_word("asc")) accept_word("desc");
    } while (accept(TokenKind::Comma));
    expect(TokenKind::RParen, "')'");
    return columns;
}

std::string Parser::data_type() {
    const Token& base = peek();
    if (base.kind != TokenKind::Word) fail_here("a data type");
    std::string type = util::to_lower(advance().text);
    if (peek_kind(TokenKind::LParen)) {
        advance();
        type += "(" + collect_until_close() + ")";
        expect(TokenKind::RParen, "')'");
    }
    while (peek_word("unsigned") || peek_word("signed") || peek_word("zerofill")) {
        type += " " + util::to_lower(advance().text);
    }
    return type;
}

ColumnDefinition Parser::column_definition(std::vector<IndexDefinition>& inline_indexes) {
    ColumnDefinition column;
    column.name = identifier("a column name");
    column.type = data_type();
    while (!at_end() && !peek_kind(TokenKind::Comma) && !peek_kind(TokenKind::RParen) &&
           !peek_kind(TokenKind::Semicolon)) {
        if (accept_word("not")) {
            if (accept_word("null")) column.nullable = false;
        } else if (accept_word("auto_increment")) {
            column.auto_increment = true;
        } else if (accept_word("primary")) {
            accept_word("key");
            column.nullable = false;
            inline_indexes.push_back({"PRIMARY", IndexKind::Primary, {column.name}});
        } else if (accept_word("unique")) {
            accept_word("key");
            inline_indexes.push_back({column.name, IndexKind::Unique, {column.name}});
        } else if (accept_word("key")) {
            column.nullable = false;
            inline_indexes.push_back({"PRIMARY", IndexKind::Primary, {column.name}});
        } else if (peek_kind(TokenKind::LParen)) {
            skip_balanced();
        } else {
            advance();
        }
    }
    return column;
}

ExprPtr Parser::expression() {
    DepthGuard guard(*this);
    auto left = or_expr();
    if (accept_op(":=")) return make<BinaryExpr>(BinaryOp::Assign, std::move(left), expression());
    return left;
}

ExprPtr Parser::or_expr() {
    auto left = xor_expr();
    while (accept_word("or") || accept_op("||")) {
        left = make<BinaryExpr>(BinaryOp::Or, std::move(left), xor_expr());
    }
    return left;
}

ExprPtr Parser::xor_expr() {
    auto left = and_expr();
    while (accept_word("xor")) {
        left = make<BinaryExpr>(BinaryOp::Xor, std::move(left), and_expr());
    }
    return left;
}

ExprPtr Parser::and_expr() {
    auto left = not_expr();
    while (accept_word("and") || accept_op("&&")) {
        left = make<BinaryExpr>(BinaryOp::And, std::move(left), not_expr());
    }
    return left;
}

ExprPtr Parser::not_expr() {
    DepthGuard guard(*this);
    if (accept_word("not")) return make<UnaryExpr>(UnaryOp::Not, not_expr());
    return predicate();
}

ExprPtr Parser::predicate() {
    auto left = bit_or();
    while (true) {
        if (accept_word("is")) {
            const bool negated = accept_word("not");
            IsTest test{};
            if (accept_word("null")) {
                test = IsTest::Null;
            } else if (accept_word("true")) {
                test = IsTest::True;
            } else if (accept_word("false")) {
                test = IsTest::False;
            } else if (accept_word("unknown")) {
                test = IsTest::Unknown;
            } else {
                fail_here("NULL, TRUE, FALSE or UNKNOWN");
            }
            left = make<IsExpr>(std::move(left), test, negated);
            continue;
        }

        bool negated = false;
        if (peek_word("not") && (peek_word("in", 1) || peek_word("like", 1) ||
                                 peek_word("between", 1) || peek_word("regexp", 1) ||
                                 peek_word("rlike", 1))) {
            advance();
            negated = true;
        }
        if (accept_word("in")) {
            left = in_tail(std::move(left), negated);
            continue;
        }
        if (accept_word("between")) {
            auto low = bit_or();
            expect_word("and");
            auto high = bit_or();
            left = make<BetweenExpr>(std::move(left), std::move(low), std::move(high), negated);
            continue;
        }
        if (accept_word("like")) {
            auto pattern = bit_or();
            if (accept_word("escape")) bit_or();
            left = make<BinaryExpr>(negated ? BinaryOp::NotLike : BinaryOp::Like, std::move(left),
                                    std::move(pattern));
            continue;
        }
        if (accept_word("regexp") || accept_word("rlike")) {
            left = make<BinaryExpr>(negated ? BinaryOp::NotRegexp : BinaryOp::Regexp,
                                    std::move(left), bit_or());
            continue;
        }

        BinaryOp op{};
        if (!comparison_op(peek(), op)) break;
        advance();
        ExprPtr right;
        if ((peek_word("any") || peek_word("some") || peek_word("all")) &&
            peek_kind(TokenKind::LParen, 1) && starts_select(2)) {
            const Quantifier q = peek_word("all") ? Quantifier::All : Quantifier::Any;
            advance();
            advance();
            auto sub = select_statement();
            expect(TokenKind::RParen, "')'");
            right = make<SubqueryExpr>(std::move(sub), q);
        } else {
            right = bit_or();
        }
        left = make<BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::in_tail(ExprPtr operand, bool negated) {
    expect(TokenKind::LParen, "'('");
    if (starts_select()) {
        auto sub = select_statement();
        expect(TokenKind::RParen, "')'");
        return make<InExpr>(std::move(operand), std::move(sub), negated);
    }
    auto values = expression_list();
    expect(TokenKind::RParen, "')'");
    return make<InExpr>(std::move(operand), std::move(values), negated);
}

ExprPtr Parser::bit_or() {
    auto left = bit_and();
    while (accept_op("|")) left = make<BinaryExpr>(BinaryOp::BitOr, std::move(left), bit_and());
    return left;
}

ExprPtr Parser::bit_and() {
    auto left = shift();
    while (accept_op("&")) left = make<BinaryExpr>(BinaryOp::BitAnd, std::move(left), shift());
    return left;
}

ExprPtr Parser::shift() {
    auto left = additive();
    while (true) {
        if (accept_op("<<")) {
            left = make<BinaryExpr>(BinaryOp::ShiftLeft, std::move(left), additive());
        } else if (accept_op(">>")) {
            left = make<BinaryExpr>(BinaryOp::ShiftRight, std::move(left), additive());
        } else {
            return left;
        }
    }
}

ExprPtr Parser::additive() {
    auto left = multiplicative();
    while (true) {
        if (accept_op("+")) {
            left = make<BinaryExpr>(BinaryOp::Add, std::move(left), multiplicative());
        } else if (accept_op("-")) {
            left = make<BinaryExpr>(BinaryOp::Sub, std::move(left), multiplicative());
        } else {
            return left;
        }
    }
}

ExprPtr Parser::multiplicative() {
    auto left = bit_xor();
    while (true) {
        BinaryOp op{};
        if (accept_op("*")) {
            op = BinaryOp::Mul;
        } else if (accept_op("/")) {
            op = BinaryOp::Div;
        } else if (accept_op("%") || accept_word("mod")) {
            op = BinaryOp::Mod;
        } else if (accept_word("div")) {
            op = BinaryOp::IntDiv;
        } else {
            return left;
        }
        left = make<BinaryExpr>(op, std::move(left), bit_xor());
    }
}

ExprPtr Parser::bit_xor() {
    auto left = unary();
    while (accept_op("^")) left = make<BinaryExpr>(BinaryOp::BitXor, std::move(left), unary());
    return left;
}

ExprPtr Parser::unary() {
    DepthGuard guard(*this);
    if (peek().is_op("-") && peek_kind(TokenKind::Number, 1)) {
        advance();
        return make<Literal>(LiteralKind::Number, "-" + std::string(advance().text));
    }
    if (accept_op("-")) return make<UnaryExpr>(UnaryOp::Negate, unary());
    if (accept_op("+")) return make<UnaryExpr>(UnaryOp::Plus, unary());
    if (accept_op("~")) return make<UnaryExpr>(UnaryOp::BitNot, unary());
    if (accept_op("!")) return make<UnaryExpr>(UnaryOp::Not, unary());
    if (peek_word("binary") && !peek_kind(TokenKind::LParen, 1)) {
        advance();
        return make<UnaryExpr>(UnaryOp::Binary, unary());
    }
    return postfix(primary());
}

ExprPtr Parser::postfix(ExprPtr e) {
    while (true) {
        if (accept_word("collate")) {
            advance();
        } else if (accept_op("->")) {
            e = make<BinaryExpr>(BinaryOp::JsonExtract, std::move(e), primary());
        } else if (accept_op("->>")) {
            e = make<BinaryExpr>(BinaryOp::JsonUnquoteExtract, std::move(e), primary());
        } else {
            return e;
        }
    }
}

ExprPtr Parser::primary() {
    const Token& t = peek();
    switch (t.kind) {
    case TokenKind::LParen: {
        if (starts_select(1)) {
            advance();
            auto q = select_statement();
            expect(TokenKind::RParen, "')'");
            return make<SubqueryExpr>(std::move(q), Quantifier::None);
        }
        advance();
        auto e = expression();
        if (accept(TokenKind::Comma)) {
            std::vector<ExprPtr> items;
            items.push_back(std::move(e));
            do {
                items.push_back(expression());
            } while (accept(TokenKind::Comma));
            expect(TokenKind::RParen, "')'");
            return make<RowExpr>(std::move(items));
        }
        expect(TokenKind::RParen, "')'");
        return e;
    }
    case TokenKind::String: return make<Literal>(LiteralKind::String, std::string(advance().text));
    case TokenKind::Number: return make<Literal>(LiteralKind::Number, std::string(advance().text));
    case TokenKind::HexNumber: return make<Literal>(LiteralKind::Hex, std::string(advance().text));
    case TokenKind::Placeholder: advance(); return make<Literal>(LiteralKind::Placeholder, "?");
    case TokenKind::Variable:
        return make<Literal>(LiteralKind::Variable, std::string(advance().text));
    case TokenKind::Word: return word_primary();
    case TokenKind::QuotedIdentifier: return column_reference();
    default: fail_here("an expression");
    }
}

ExprPtr Parser::word_primary() {
    const Token& t = peek();
    const std::string_view w = t.text;
    const bool call = peek_kind(TokenKind::LParen, 1);

    if (util::iequals(w, "null")) {
        advance();
        return make<Literal>(LiteralKind::Null, "NULL");
    }
    if (util::iequals(w, "true") || util::iequals(w, "false")) {
        return make<Literal>(LiteralKind::Boolean, util::to_upper(advance().text));
    }
    if (util::iequals(w, "default") && !call) {
        advance();
        return make<Literal>(LiteralKind::Keyword, "DEFAULT");
    }
    if (util::iequals(w, "exists") && call) {
        advance();
        advance();
        auto q = select_statement();
        expect(TokenKind::RParen, "')'");
        return make<SubqueryExpr>(std::move(q), Quantifier::Exists);
    }
    if (util::iequals(w, "case")) return case_expr();
    if (util::iequals(w, "cast") && call) return cast_expr();
    if (util::iequals(w, "convert") && call) return convert_expr();
    if (util::iequals(w, "match") && call) return match_expr();
    if (util::iequals(w, "interval")) {
        advance();
        auto value = bit_or();
        const Token& unit = advance();
        if (unit.kind != TokenKind::Word) fail("expected an interval unit");
        return make<IntervalExpr>(std::move(value), util::to_upper(unit.text));
    }
    if ((util::iequals(w, "date") || util::iequals(w, "time") || util::iequals(w, "timestamp")) &&
        peek_kind(TokenKind::String, 1)) {
        advance();
        return make<Literal>(LiteralKind::String, std::string(advance().text));
    }
    if (is_niladic_function(w) && !call) {
        return make<FunctionCall>(util::to_lower(advance().text), std::vector<ExprPtr>{});
    }
    if (call) {
        std::string name = util::to_lower(advance().text);
        return function_call(std::move(name));
    }
    if (is_reserved_word(w) && !usable_as_bare_identifier(w)) fail_here("an expression");
    return column_reference();
}

ExprPtr Parser::function_call(std::string name) {
    expect(TokenKind::LParen, "'('");
    std::vector<ExprPtr> args;
    bool star = false;
    bool distinct = false;

    if (accept_op("*")) {
        star = true;
    } else if (!peek_kind(TokenKind::RParen)) {
        if (accept_word("distinct")) {
            distinct = true;
        } else {
            accept_word("all");
        }
        if (name == "position") {
            args.push_back(bit_or());
            expect_word("in");
            args.push_back(expression());
        } else if (name == "extract") {
            const Token& unit = advance();
            args.push_back(make<Literal>(LiteralKind::Keyword, util::to_upper(unit.text)));
            expect_word("from");
            args.push_back(expression());
        } else {
            auto more = [&]() -> bool {
                if (accept(TokenKind::Comma) || accept_word("from") || accept_word("for") ||
                    accept_word("separator")) {
                    return true;
                }
                if (accept_word("using")) {
                    args.push_back(make<Literal>(LiteralKind::Keyword, std::string(advance().text)));
                    return accept(TokenKind::Comma);
                }
                if (peek_word("order") && peek_word("by", 1)) {
                    advance();
                    advance();
                    for (auto& item : order_by_list()) args.push_back(std::move(item.expr));
                    return accept_word("separator");
                }
                return false;
            };
            do {
                while (accept_word("leading") || accept_word("trailing") || accept_word("both")) {
                }
                accept_word("from");
                args.push_back(expression());
            } while (more());
        }
    }
    expect(TokenKind::RParen, "')'");

    bool windowed = false;
    if (accept_word("over")) {
        windowed = true;
        if (peek_kind(TokenKind::LParen)) {
            skip_balanced();
        } else {
            identifier("a window name");
        }
    }
    return make<FunctionCall>(std::move(name), std::move(args), star, distinct, windowed);
}

ExprPtr Parser::case_expr() {
    expect_word("case");
    ExprPtr operand;
    if (!peek_word("when")) operand = expression();
    std::vector<WhenClause> branches;
    while (accept_word("when")) {
        WhenClause branch;
        branch.condition = expression();
        expect_word("then");
        branch.result = expression();
        branches.push_back(std::move(branch));
    }
    if (branches.empty()) fail_here("WHEN");
    ExprPtr else_result;
    if (accept_word("else")) else_result = expression();
    expect_word("end");
    return make<CaseExpr>(std::move(operand), std::move(branches), std::move(else_result));
}

ExprPtr Parser::cast_expr() {
    advance();
    expect(TokenKind::LParen, "'('");
    auto operand = expression();
    expect_word("as");
    std::string type = collect_until_close();
    expect(TokenKind::RParen, "')'");
    return make<CastExpr>(std::move(operand), std::move(type));
}

ExprPtr Parser::convert_expr() {
    advance();
    expect(TokenKind::LParen, "'('");
    auto operand = expression();
    std::string type;
    if (accept(TokenKind::Comma)) {
        type = collect_until_close();
    } else if (accept_word("using")) {
        type = "using " + collect_until_close();
    } else {
        fail_here("',' or USING");
    }
    expect(TokenKind::RParen, "')'");
    return make<CastExpr>(std::move(operand), std::move(type));
}

ExprPtr Parser::match_expr() {
    advance();
    expect(TokenKind::LParen, "'('");
    auto args = expression_list();
    expect(TokenKind::RParen, "')'");
    expect_word("against");
    expect(TokenKind::LParen, "'('");
    args.push_back(bit_or());
    while (!at_end() && !peek_kind(TokenKind::RParen)) advance();
    expect(TokenKind::RParen, "')'");
    return make<FunctionCall>("match", std::move(args));
}

std::vector<ExprPtr> Parser::expression_list() {
    std::vector<ExprPtr> out;
    do {
        out.push_back(expression());
    } while (accept(TokenKind::Comma));
    return out;
}

StatementPtr parse(std::string_view sql) {
    Parser parser(sql);
    return parser.parse_statement();
}

StatementPtr try_parse(std::string_view sql, std::string* error) {
    try {
        return parse(sql);
    } catch (const ParseError& e) {
        if (error != nullptr) *error = e.what();
        return nullptr;
    }
}

}
