#include <gtest/gtest.h>

#include <string>

#include "snailtrail/sql/parser.hpp"

using namespace snailtrail::sql;

namespace {

template <typename T>
const T& as(const Statement& s) {
    const auto* p = dynamic_cast<const T*>(&s);
    if (p == nullptr) throw std::runtime_error("unexpected statement type");
    return *p;
}

template <typename T>
const T& as(const Expr& e) {
    const auto* p = dynamic_cast<const T*>(&e);
    if (p == nullptr) throw std::runtime_error("unexpected expression type");
    return *p;
}

template <typename T>
std::unique_ptr<T> parse_as(std::string_view sql) {
    auto s = parse(sql);
    auto* raw = dynamic_cast<T*>(s.get());
    if (raw == nullptr) throw std::runtime_error("unexpected statement type");
    s.release();
    return std::unique_ptr<T>(raw);
}

std::unique_ptr<SelectStatement> select(std::string_view sql) { return parse_as<SelectStatement>(sql); }

ExprPtr expr(std::string_view sql) {
    Parser p(sql);
    return p.parse_expression();
}

}

TEST(Parser, ParsesASimpleSelect) {
    auto s = select("SELECT id, name FROM users WHERE id = 42");
    ASSERT_EQ(s->items().size(), 2U);
    EXPECT_EQ(as<ColumnRef>(*s->items()[0].expr).name(), "id");
    ASSERT_EQ(s->from().size(), 1U);
    EXPECT_EQ(s->from()[0].name, "users");
    const auto& where = as<BinaryExpr>(*s->where());
    EXPECT_EQ(where.op(), BinaryOp::Eq);
    EXPECT_EQ(as<ColumnRef>(where.left()).name(), "id");
    EXPECT_EQ(as<Literal>(where.right()).number(), 42.0);
}

TEST(Parser, ReadsAliasesWithAndWithoutAs) {
    auto s = select("SELECT o.id AS order_id, c.name customer FROM orders o "
                    "JOIN customers AS c ON c.id = o.customer_id");
    EXPECT_EQ(s->items()[0].alias, "order_id");
    EXPECT_EQ(s->items()[1].alias, "customer");
    EXPECT_EQ(s->from()[0].alias, "o");
    EXPECT_EQ(s->from()[0].reference_name(), "o");
    ASSERT_EQ(s->joins().size(), 1U);
    EXPECT_EQ(s->joins()[0].kind, JoinKind::Inner);
    EXPECT_EQ(s->joins()[0].table.alias, "c");
    const auto& on = as<BinaryExpr>(*s->joins()[0].condition);
    EXPECT_EQ(as<ColumnRef>(on.left()).table(), "c");
}

TEST(Parser, ReadsEveryJoinFlavour) {
    auto s = select("SELECT * FROM a LEFT OUTER JOIN b ON b.a_id = a.id RIGHT JOIN c USING (x, y) "
                    "CROSS JOIN d STRAIGHT_JOIN e ON e.id = d.e_id NATURAL JOIN f, g");
    ASSERT_EQ(s->joins().size(), 5U);
    EXPECT_EQ(s->joins()[0].kind, JoinKind::Left);
    EXPECT_EQ(s->joins()[1].kind, JoinKind::Right);
    EXPECT_EQ(s->joins()[1].using_columns, (std::vector<std::string>{"x", "y"}));
    EXPECT_EQ(s->joins()[2].kind, JoinKind::Cross);
    EXPECT_EQ(s->joins()[3].kind, JoinKind::Straight);
    EXPECT_TRUE(s->joins()[4].natural);
    ASSERT_EQ(s->from().size(), 2U);
    EXPECT_EQ(s->from()[1].name, "g");
}

TEST(Parser, ReadsSchemaQualifiedNamesAndStars) {
    auto s = select("SELECT t.*, shop.orders.id, * FROM shop.orders t");
    EXPECT_TRUE(s->items()[0].star);
    EXPECT_EQ(s->items()[0].star_qualifier, "t");
    const auto& col = as<ColumnRef>(*s->items()[1].expr);
    EXPECT_EQ(col.schema(), "shop");
    EXPECT_EQ(col.table(), "orders");
    EXPECT_TRUE(s->items()[2].star);
    EXPECT_EQ(s->from()[0].schema, "shop");
}

TEST(Parser, ReadsGroupingOrderingAndLimits) {
    auto s = select("SELECT customer_id, COUNT(*) n FROM orders GROUP BY customer_id WITH ROLLUP "
                    "HAVING COUNT(*) > 5 ORDER BY n DESC, customer_id LIMIT 20 OFFSET 40");
    ASSERT_EQ(s->group_by().size(), 1U);
    EXPECT_TRUE(s->with_rollup());
    ASSERT_NE(s->having(), nullptr);
    ASSERT_EQ(s->order_by().size(), 2U);
    EXPECT_TRUE(s->order_by()[0].descending);
    EXPECT_FALSE(s->order_by()[1].descending);
    ASSERT_TRUE(s->limit().has_value());
    EXPECT_EQ(as<Literal>(*s->limit()->count).text(), "20");
    EXPECT_EQ(as<Literal>(*s->limit()->offset).text(), "40");

    auto mysql_style = select("SELECT a FROM t LIMIT 10000, 25");
    EXPECT_EQ(as<Literal>(*mysql_style->limit()->offset).text(), "10000");
    EXPECT_EQ(as<Literal>(*mysql_style->limit()->count).text(), "25");
}

TEST(Parser, ReadsLockingClauses) {
    EXPECT_EQ(select("SELECT * FROM t WHERE id = 1 FOR UPDATE")->lock_mode(), LockMode::ForUpdate);
    EXPECT_EQ(select("SELECT * FROM t FOR SHARE SKIP LOCKED")->lock_mode(), LockMode::ForShare);
    EXPECT_EQ(select("SELECT * FROM t LOCK IN SHARE MODE")->lock_mode(), LockMode::ForShare);
    EXPECT_EQ(select("SELECT * FROM t FOR UPDATE OF t NOWAIT")->lock_mode(), LockMode::ForUpdate);
}

TEST(Parser, ReadsSetOperations) {
    auto s = select("SELECT a FROM t UNION ALL SELECT a FROM u UNION SELECT a FROM v ORDER BY a");
    ASSERT_EQ(s->set_operations().size(), 2U);
    EXPECT_EQ(s->set_operations()[0].op, SetOperator::UnionAll);
    EXPECT_EQ(s->set_operations()[1].op, SetOperator::Union);

    auto parenthesised = select("(SELECT a FROM t) UNION (SELECT a FROM u) LIMIT 5");
    EXPECT_EQ(parenthesised->set_operations().size(), 1U);
    EXPECT_TRUE(parenthesised->limit().has_value());
}

TEST(Parser, ReadsCommonTableExpressions) {
    auto s = select("WITH RECURSIVE big (id) AS (SELECT id FROM orders WHERE total > 100), "
                    "recent AS (SELECT * FROM big) SELECT * FROM recent");
    ASSERT_EQ(s->ctes().size(), 2U);
    EXPECT_EQ(s->ctes()[0].name, "big");
    EXPECT_EQ(s->ctes()[0].columns, (std::vector<std::string>{"id"}));
    EXPECT_EQ(s->from()[0].name, "recent");
}

TEST(Parser, ReadsSubqueriesEverywhere) {
    auto s = select("SELECT (SELECT MAX(total) FROM orders) AS top, d.n FROM "
                    "(SELECT COUNT(*) n FROM users) AS d WHERE EXISTS (SELECT 1 FROM x) "
                    "AND id IN (SELECT user_id FROM bans) AND score > ALL (SELECT score FROM s)");
    EXPECT_EQ(as<SubqueryExpr>(*s->items()[0].expr).quantifier(), Quantifier::None);
    ASSERT_TRUE(s->from()[0].is_derived());
    EXPECT_EQ(s->from()[0].alias, "d");
    const auto& all = as<BinaryExpr>(as<BinaryExpr>(*s->where()).right());
    EXPECT_EQ(as<SubqueryExpr>(all.right()).quantifier(), Quantifier::All);
    const auto& left = as<BinaryExpr>(as<BinaryExpr>(*s->where()).left());
    EXPECT_EQ(as<SubqueryExpr>(left.left()).quantifier(), Quantifier::Exists);
    EXPECT_NE(as<InExpr>(left.right()).subquery(), nullptr);
}

TEST(Parser, RespectsMySqlOperatorPrecedence) {
    auto e = expr("a = 1 OR b = 2 AND c = 3");
    const auto& root = as<BinaryExpr>(*e);
    EXPECT_EQ(root.op(), BinaryOp::Or);
    EXPECT_EQ(as<BinaryExpr>(root.right()).op(), BinaryOp::And);

    auto n = expr("NOT a = 1");
    const auto& not_node = as<UnaryExpr>(*n);
    EXPECT_EQ(not_node.op(), UnaryOp::Not);
    EXPECT_EQ(as<BinaryExpr>(not_node.operand()).op(), BinaryOp::Eq);

    auto arith = expr("a + b * c - d");
    const auto& sub = as<BinaryExpr>(*arith);
    EXPECT_EQ(sub.op(), BinaryOp::Sub);
    EXPECT_EQ(as<BinaryExpr>(sub.left()).op(), BinaryOp::Add);
    EXPECT_EQ(as<BinaryExpr>(as<BinaryExpr>(sub.left()).right()).op(), BinaryOp::Mul);

    auto between = expr("x BETWEEN 1 AND 5 AND y = 2");
    const auto& and_node = as<BinaryExpr>(*between);
    EXPECT_EQ(and_node.op(), BinaryOp::And);
    EXPECT_NO_THROW(as<BetweenExpr>(and_node.left()));
}

TEST(Parser, ReadsPredicates) {
    auto e = expr("a IS NOT NULL AND b NOT IN (1, 2, 3) AND c NOT LIKE '%x' ESCAPE '!' "
                  "AND d NOT BETWEEN 1 AND 2 AND e <=> NULL AND f REGEXP '^a'");
    std::vector<const Expr*> conjuncts;
    const Expr* cursor = e.get();
    while (const auto* b = dynamic_cast<const BinaryExpr*>(cursor)) {
        if (b->op() != BinaryOp::And) break;
        conjuncts.insert(conjuncts.begin(), &b->right());
        cursor = &b->left();
    }
    conjuncts.insert(conjuncts.begin(), cursor);
    ASSERT_EQ(conjuncts.size(), 6U);
    EXPECT_TRUE(as<IsExpr>(*conjuncts[0]).negated());
    EXPECT_EQ(as<InExpr>(*conjuncts[1]).values().size(), 3U);
    EXPECT_TRUE(as<InExpr>(*conjuncts[1]).negated());
    EXPECT_EQ(as<BinaryExpr>(*conjuncts[2]).op(), BinaryOp::NotLike);
    EXPECT_EQ(as<Literal>(as<BinaryExpr>(*conjuncts[2]).right()).value(), "%x");
    EXPECT_TRUE(as<BetweenExpr>(*conjuncts[3]).negated());
    EXPECT_EQ(as<BinaryExpr>(*conjuncts[4]).op(), BinaryOp::NullSafeEq);
    EXPECT_EQ(as<BinaryExpr>(*conjuncts[5]).op(), BinaryOp::Regexp);
}

TEST(Parser, ReadsFunctionsAndSpecialForms) {
    auto s = select(
        "SELECT COUNT(*), COUNT(DISTINCT a), DATE(created_at), CAST(p AS DECIMAL(10, 2)), "
        "CONVERT(n USING utf8mb4), EXTRACT(YEAR FROM d), TRIM(LEADING '0' FROM code), "
        "SUBSTRING(s FROM 2 FOR 3), GROUP_CONCAT(DISTINCT tag ORDER BY tag SEPARATOR ','), "
        "ROW_NUMBER() OVER (PARTITION BY a ORDER BY b), POSITION('x' IN s), "
        "CURRENT_TIMESTAMP, NOW() - INTERVAL 7 DAY FROM t");
    const auto& items = s->items();
    ASSERT_EQ(items.size(), 13U);
    EXPECT_TRUE(as<FunctionCall>(*items[0].expr).star());
    EXPECT_TRUE(as<FunctionCall>(*items[0].expr).is_aggregate());
    EXPECT_TRUE(as<FunctionCall>(*items[1].expr).distinct());
    EXPECT_EQ(as<FunctionCall>(*items[2].expr).name(), "date");
    EXPECT_EQ(as<CastExpr>(*items[3].expr).target_type(), "decimal(10,2)");
    EXPECT_EQ(as<CastExpr>(*items[4].expr).target_type(), "using utf8mb4");
    EXPECT_EQ(as<FunctionCall>(*items[5].expr).args().size(), 2U);
    EXPECT_EQ(as<FunctionCall>(*items[6].expr).args().size(), 2U);
    EXPECT_EQ(as<FunctionCall>(*items[7].expr).args().size(), 3U);
    EXPECT_EQ(as<FunctionCall>(*items[8].expr).args().size(), 3U);
    EXPECT_TRUE(as<FunctionCall>(*items[9].expr).windowed());
    EXPECT_FALSE(as<FunctionCall>(*items[9].expr).is_aggregate());
    EXPECT_EQ(as<FunctionCall>(*items[10].expr).name(), "position");
    EXPECT_EQ(as<FunctionCall>(*items[11].expr).name(), "current_timestamp");
    const auto& minus = as<BinaryExpr>(*items[12].expr);
    EXPECT_EQ(as<IntervalExpr>(minus.right()).unit(), "DAY");
}

TEST(Parser, ReadsCaseMatchJsonAndVariables) {
    auto s = select("SELECT CASE WHEN a > 1 THEN 'big' ELSE 'small' END, CASE s WHEN 1 THEN 'x' END, "
                    "doc->>'$.name', @rank := @rank + 1, ? FROM t "
                    "WHERE MATCH(title, body) AGAINST ('mysql' IN BOOLEAN MODE)");
    EXPECT_EQ(as<CaseExpr>(*s->items()[0].expr).branches().size(), 1U);
    EXPECT_NE(as<CaseExpr>(*s->items()[0].expr).else_result(), nullptr);
    EXPECT_NE(as<CaseExpr>(*s->items()[1].expr).operand(), nullptr);
    EXPECT_EQ(as<BinaryExpr>(*s->items()[2].expr).op(), BinaryOp::JsonUnquoteExtract);
    EXPECT_EQ(as<BinaryExpr>(*s->items()[3].expr).op(), BinaryOp::Assign);
    EXPECT_EQ(as<Literal>(*s->items()[4].expr).kind(), LiteralKind::Placeholder);
    const auto& match = as<FunctionCall>(*s->where());
    EXPECT_EQ(match.name(), "match");
    EXPECT_EQ(match.args().size(), 3U);
}

TEST(Parser, NonReservedKeywordsAreColumnNames) {
    auto s = select("SELECT status, date, comment, level, `order`, end FROM t "
                    "WHERE status = 'x' AND date > '2024-01-01'");
    EXPECT_EQ(s->items().size(), 6U);
    EXPECT_EQ(as<ColumnRef>(*s->items()[4].expr).name(), "order");
    EXPECT_EQ(as<ColumnRef>(*s->items()[5].expr).name(), "end");
}

TEST(Parser, FoldsNegativeNumbers) {
    auto e = expr("x > -5");
    EXPECT_EQ(as<Literal>(as<BinaryExpr>(*e).right()).number(), -5.0);
}

TEST(Parser, ReadsInserts) {
    auto s = parse("INSERT IGNORE INTO shop.orders (customer_id, total) VALUES (1, 9.5), (2, 3) "
                   "ON DUPLICATE KEY UPDATE total = VALUES(total)");
    const auto& insert = as<InsertStatement>(*s);
    EXPECT_EQ(insert.kind(), StatementKind::Insert);
    EXPECT_TRUE(insert.ignore());
    EXPECT_EQ(insert.table().schema, "shop");
    EXPECT_EQ(insert.columns(), (std::vector<std::string>{"customer_id", "total"}));
    EXPECT_EQ(insert.row_count(), 2U);
    EXPECT_EQ(insert.on_duplicate_key_update().size(), 1U);

    const auto select_form = parse_as<InsertStatement>("INSERT INTO archive SELECT * FROM t");
    EXPECT_NE(select_form->select(), nullptr);

    const auto set_form = parse_as<InsertStatement>("REPLACE INTO t SET a = 1, b = 2");
    EXPECT_EQ(set_form->kind(), StatementKind::Replace);
    EXPECT_EQ(set_form->assignments().size(), 2U);
}

TEST(Parser, ReadsUpdates) {
    const auto u = parse_as<UpdateStatement>(
        "UPDATE orders o JOIN customers c ON c.id = o.customer_id SET o.status = 'vip', "
        "o.total = o.total * 0.9 WHERE c.tier = 3 ORDER BY o.id LIMIT 100");
    EXPECT_EQ(u->tables()[0].alias, "o");
    EXPECT_EQ(u->joins().size(), 1U);
    EXPECT_EQ(u->assignments().size(), 2U);
    EXPECT_EQ(u->assignments()[0].column->table(), "o");
    EXPECT_NE(u->where(), nullptr);
    EXPECT_EQ(u->order_by().size(), 1U);
    EXPECT_TRUE(u->limit().has_value());
}

TEST(Parser, ReadsDeletes) {
    const auto single =
        parse_as<DeleteStatement>("DELETE FROM sessions WHERE expires_at < NOW()");
    EXPECT_EQ(single->from()[0].name, "sessions");
    EXPECT_TRUE(single->targets().empty());

    const auto multi =
        parse_as<DeleteStatement>("DELETE t1, t2.* FROM t1 JOIN t2 ON t2.id = t1.t2_id WHERE t1.x = 1");
    EXPECT_EQ(multi->targets(), (std::vector<std::string>{"t1", "t2"}));
    EXPECT_EQ(multi->joins().size(), 1U);

    const auto using_form =
        parse_as<DeleteStatement>("DELETE FROM a USING a JOIN b ON b.id = a.b_id WHERE b.x = 1");
    EXPECT_EQ(using_form->targets(), (std::vector<std::string>{"a"}));

    const auto cte = parse_as<DeleteStatement>(
        "WITH old AS (SELECT id FROM t WHERE d < '2020-01-01') DELETE FROM t WHERE id IN "
        "(SELECT id FROM old)");
    EXPECT_NE(cte->where(), nullptr);
}

TEST(Parser, ReadsMysqldumpCreateTable) {
    const auto t = parse_as<CreateTableStatement>(R"(
CREATE TABLE IF NOT EXISTS `orders` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `customer_id` int NOT NULL,
  `status` enum('new','paid','sent') COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'new',
  `phone` varchar(20) DEFAULT NULL COMMENT 'not null here',
  `total` decimal(10,2) NOT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_phone` (`phone`),
  KEY `idx_customer` (`customer_id`,`created_at` DESC),
  KEY `idx_email_prefix` (`phone`(8)),
  FULLTEXT KEY `ft_status` (`status`),
  KEY `idx_lower` ((lower(`status`))),
  CONSTRAINT `fk_customer` FOREIGN KEY (`customer_id`) REFERENCES `customers` (`id`) ON DELETE CASCADE,
  CONSTRAINT `chk_total` CHECK ((`total` >= 0))
) ENGINE=InnoDB AUTO_INCREMENT=42 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci)");
    EXPECT_EQ(t->table().name, "orders");
    ASSERT_EQ(t->columns().size(), 6U);
    EXPECT_EQ(t->columns()[0].type, "bigint unsigned");
    EXPECT_TRUE(t->columns()[0].auto_increment);
    EXPECT_FALSE(t->columns()[0].nullable);
    EXPECT_EQ(t->columns()[2].type, "enum('new','paid','sent')");
    EXPECT_EQ(t->columns()[3].type, "varchar(20)");
    EXPECT_TRUE(t->columns()[3].nullable);
    EXPECT_EQ(t->columns()[4].type, "decimal(10,2)");

    ASSERT_EQ(t->indexes().size(), 6U);
    EXPECT_EQ(t->indexes()[0].kind, IndexKind::Primary);
    EXPECT_EQ(t->indexes()[1].kind, IndexKind::Unique);
    EXPECT_EQ(t->indexes()[1].name, "uq_phone");
    EXPECT_EQ(t->indexes()[2].columns, (std::vector<std::string>{"customer_id", "created_at"}));
    EXPECT_EQ(t->indexes()[3].columns, (std::vector<std::string>{"phone"}));
    EXPECT_EQ(t->indexes()[4].kind, IndexKind::Fulltext);
    EXPECT_EQ(t->indexes()[5].columns, (std::vector<std::string>{"(LOWER(status))"}));
}

TEST(Parser, ReadsInlineKeysAndIndexStatements) {
    const auto t = parse_as<CreateTableStatement>(
        "CREATE TEMPORARY TABLE t (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE)");
    EXPECT_TRUE(t->temporary());
    ASSERT_EQ(t->indexes().size(), 2U);
    EXPECT_EQ(t->indexes()[0].kind, IndexKind::Primary);
    EXPECT_EQ(t->indexes()[1].kind, IndexKind::Unique);

    const auto create_index =
        parse_as<AlterTableStatement>("CREATE UNIQUE INDEX uq_email ON users (email)");
    EXPECT_EQ(create_index->table().name, "users");
    ASSERT_EQ(create_index->added_indexes().size(), 1U);
    EXPECT_EQ(create_index->added_indexes()[0].kind, IndexKind::Unique);

    const auto alter = parse_as<AlterTableStatement>(
        "ALTER TABLE orders ADD INDEX idx_status (status, created_at), DROP INDEX idx_old, "
        "ADD COLUMN note TEXT AFTER total, ADD CONSTRAINT uq UNIQUE KEY (code)");
    EXPECT_EQ(alter->added_indexes().size(), 2U);
    EXPECT_EQ(alter->dropped_indexes(), (std::vector<std::string>{"idx_old"}));
    ASSERT_EQ(alter->added_columns().size(), 1U);
    EXPECT_EQ(alter->added_columns()[0].name, "note");
}

TEST(Parser, ClassifiesOtherStatements) {
    EXPECT_EQ(parse("SET NAMES utf8mb4")->kind(), StatementKind::Utility);
    EXPECT_EQ(parse("SHOW FULL PROCESSLIST")->kind(), StatementKind::Utility);
    EXPECT_EQ(parse("BEGIN")->kind(), StatementKind::Transaction);
    EXPECT_EQ(parse("DROP TABLE IF EXISTS t")->kind(), StatementKind::Ddl);
    EXPECT_EQ(parse_as<OtherStatement>("COMMIT;")->keyword(), "commit");
}

TEST(Parser, ParsesScriptsAndCollectsErrors) {
    const std::string dump = R"(
/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
DROP TABLE IF EXISTS `t`;
CREATE TABLE `t` (`id` int NOT NULL, PRIMARY KEY (`id`)) ENGINE=InnoDB;
LOCK TABLES `t` WRITE;
SELECT FROM nowhere;
UNLOCK TABLES;
)";
    Parser p(dump);
    std::vector<ParseError> errors;
    const auto statements = p.parse_script(&errors);
    ASSERT_EQ(statements.size(), 5U);
    EXPECT_EQ(statements[0]->kind(), StatementKind::Utility);
    EXPECT_NO_THROW(static_cast<void>(as<CreateTableStatement>(*statements[2])));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_GT(errors[0].offset(), 0U);
}

TEST(Parser, ReportsErrorsWithPositions) {
    try {
        parse("SELECT a FROM t WHERE");
        FAIL() << "expected a ParseError";
    } catch (const ParseError& e) {
        EXPECT_EQ(e.offset(), 21U);
        EXPECT_NE(std::string(e.what()).find("end of statement"), std::string::npos);
    }
    EXPECT_THROW(parse("SELECT FROM t"), ParseError);
    EXPECT_THROW(parse("SELECT 1 2"), ParseError);
    EXPECT_THROW(parse(""), ParseError);
    EXPECT_THROW(parse("SELECT (1"), ParseError);

    std::string error;
    EXPECT_EQ(try_parse("UPDATE t SET", &error), nullptr);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(try_parse("SELECT 1"), nullptr);
}

TEST(Parser, SurvivesPathologicalNesting) {
    const std::string deep = "SELECT " + std::string(5000, '(') + "1" + std::string(5000, ')');
    EXPECT_THROW(parse(deep), ParseError);

    std::string signs = "SELECT ";
    for (int i = 0; i < 5000; ++i) signs += "- ";
    signs += "x";
    EXPECT_THROW(parse(signs), ParseError);

    std::string nots = "SELECT * FROM t WHERE ";
    for (int i = 0; i < 5000; ++i) nots += "NOT ";
    nots += "a";
    EXPECT_THROW(parse(nots), ParseError);

    const std::string fine = "SELECT " + std::string(40, '(') + "1" + std::string(40, ')');
    EXPECT_NO_THROW(parse(fine));
}
