#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "snailtrail/sql/ast_printer.hpp"
#include "snailtrail/sql/parser.hpp"
#include "snailtrail/sql/sql_writer.hpp"

using namespace snailtrail::sql;

namespace {

std::string roundtrip(std::string_view sql) {
    auto s = parse(sql);
    const auto* select = dynamic_cast<const SelectStatement*>(s.get());
    if (select == nullptr) throw std::runtime_error("not a SELECT");
    return to_sql(*select);
}

std::string expr_sql(std::string_view sql) {
    Parser p(sql);
    return to_sql(*p.parse_expression());
}

}

TEST(SqlWriter, WritesCanonicalSelects) {
    EXPECT_EQ(roundtrip("select a,b from t where x=1 and (y=2 or z=3)"),
              "SELECT a, b FROM t WHERE x = 1 AND (y = 2 OR z = 3)");
    EXPECT_EQ(roundtrip("SELECT DISTINCT o.id FROM orders o LEFT JOIN items i ON i.order_id = o.id "
                        "JOIN tags USING (tag_id) WHERE i.id IS NULL ORDER BY o.id DESC LIMIT 10, 5"),
              "SELECT DISTINCT o.id FROM orders AS o LEFT JOIN items AS i ON i.order_id = o.id "
              "JOIN tags USING (tag_id) WHERE i.id IS NULL ORDER BY o.id DESC LIMIT 10, 5");
}

TEST(SqlWriter, AddsOnlyTheParenthesesPrecedenceNeeds) {
    EXPECT_EQ(expr_sql("NOT (a = 1 AND b = 2)"), "NOT (a = 1 AND b = 2)");
    EXPECT_EQ(expr_sql("NOT a = 1"), "NOT a = 1");
    EXPECT_EQ(expr_sql("(a - b) - c"), "a - b - c");
    EXPECT_EQ(expr_sql("a - (b - c)"), "a - (b - c)");
    EXPECT_EQ(expr_sql("(a + b) * c"), "(a + b) * c");
    EXPECT_EQ(expr_sql("a OR b AND c"), "a OR b AND c");
    EXPECT_EQ(expr_sql("(a OR b) AND c"), "(a OR b) AND c");
    EXPECT_EQ(expr_sql("-(a + 1)"), "-(a + 1)");
    EXPECT_EQ(expr_sql("x > -5"), "x > -5");
}

TEST(SqlWriter, QuotesIdentifiersOnlyWhenNeeded) {
    EXPECT_EQ(roundtrip("SELECT `order`, `select`, `plain`, `we``ird` FROM `my table`"),
              "SELECT `order`, `select`, plain, `we``ird` FROM `my table`");
    EXPECT_EQ(quote_identifier("123"), "`123`");
    EXPECT_EQ(quote_identifier("1st"), "1st");
    EXPECT_EQ(quote_identifier("ação"), "ação");
}

TEST(SqlWriter, WritesFunctionsAndSpecialForms) {
    EXPECT_EQ(roundtrip("select count(*), count(distinct a), date(created_at), "
                        "cast(p as decimal(10, 2)), convert(n using utf8mb4), extract(year from d), "
                        "d + interval 1 day, position('x' in s), current_timestamp from t"),
              "SELECT COUNT(*), COUNT(DISTINCT a), DATE(created_at), CAST(p AS DECIMAL(10,2)), "
              "CONVERT(n USING utf8mb4), EXTRACT(YEAR FROM d), d + INTERVAL 1 DAY, "
              "POSITION('x' IN s), CURRENT_TIMESTAMP FROM t");
    EXPECT_EQ(expr_sql("CASE WHEN a > 1 THEN 'x' ELSE 'y' END"),
              "CASE WHEN a > 1 THEN 'x' ELSE 'y' END");
    EXPECT_EQ(expr_sql("MATCH(a, b) AGAINST('q' IN BOOLEAN MODE)"), "MATCH(a, b) AGAINST('q')");
    EXPECT_EQ(expr_sql("doc->>'$.name'"), "doc->>'$.name'");
}

TEST(SqlWriter, WritesPredicates) {
    EXPECT_EQ(expr_sql("a NOT IN (1,2) AND b BETWEEN 1 AND 5 AND c IS NOT NULL AND d NOT LIKE 'x%'"),
              "a NOT IN (1, 2) AND b BETWEEN 1 AND 5 AND c IS NOT NULL AND d NOT LIKE 'x%'");
}

TEST(SqlWriter, WritesSubqueriesAndCtes) {
    EXPECT_EQ(roundtrip("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM u WHERE u.t_id = t.id) "
                        "AND a > ANY (SELECT b FROM v) AND c IN (SELECT c FROM w)"),
              "SELECT * FROM t WHERE EXISTS (SELECT 1 FROM u WHERE u.t_id = t.id) "
              "AND a > ANY (SELECT b FROM v) AND c IN (SELECT c FROM w)");
    EXPECT_EQ(roundtrip("with r as (select id from t) select * from r union all select id from u "
                        "order by id limit 3"),
              "WITH r AS (SELECT id FROM t) SELECT * FROM r UNION ALL SELECT id FROM u "
              "ORDER BY id LIMIT 3");
    EXPECT_EQ(roundtrip("SELECT n FROM (SELECT COUNT(*) AS n FROM t) d FOR UPDATE"),
              "SELECT n FROM (SELECT COUNT(*) AS n FROM t) AS d FOR UPDATE");
}

TEST(SqlWriter, OutputParsesBackToTheSameText) {
    const std::vector<std::string> corpus = {
        "SELECT a FROM t WHERE (a = 1 OR a = 2) AND NOT (b < 3 OR c > 4)",
        "SELECT o.id, SUM(i.qty * i.price) AS total FROM orders o JOIN items i ON i.o = o.id "
        "GROUP BY o.id HAVING SUM(i.qty) > 10 ORDER BY total DESC LIMIT 5",
        "SELECT * FROM t WHERE a - (b - c) > 0 AND d / (e * f) < 1 AND x IN (SELECT y FROM z)",
        "SELECT CASE WHEN a THEN b END, IF(x, 1, 2), COALESCE(a, b, c) FROM t",
    };
    for (const auto& sql : corpus) {
        const std::string once = roundtrip(sql);
        EXPECT_EQ(roundtrip(once), once) << sql;
    }
}

TEST(AstPrinter, DrawsATree) {
    auto s = parse("SELECT a FROM t WHERE b = 1");
    EXPECT_EQ(dump_ast(*s),
              "SELECT\n"
              "├── items\n"
              "│   └── Column a\n"
              "├── FROM\n"
              "│   └── Table t\n"
              "└── WHERE\n"
              "    └── =\n"
              "        ├── Column b\n"
              "        └── Number 1\n");
}

TEST(AstPrinter, DescribesEveryStatementKind) {
    for (const char* sql :
         {"INSERT INTO t (a) VALUES (1), (2) ON DUPLICATE KEY UPDATE a = 3",
          "UPDATE t SET a = 1 WHERE b IN (1, 2) ORDER BY c LIMIT 1", "DELETE FROM t WHERE a <=> NULL",
          "CREATE TABLE t (id INT PRIMARY KEY, KEY k (a, b))", "ALTER TABLE t ADD INDEX i (a)",
          "SHOW TABLES",
          "SELECT CASE WHEN a THEN CAST(b AS CHAR) END, EXISTS (SELECT 1), ROW_NUMBER() OVER (), "
          "(1, 2), -a, a BETWEEN 1 AND 2, x - INTERVAL 1 DAY FROM (SELECT 1) AS d"}) {
        const std::string tree = dump_ast(*parse(sql));
        EXPECT_FALSE(tree.empty()) << sql;
        EXPECT_EQ(tree.back(), '\n');
    }
    EXPECT_NE(dump_ast(*parse("INSERT INTO t VALUES (1), (2)")).find("VALUES: 2 row(s)"),
              std::string::npos);
}
