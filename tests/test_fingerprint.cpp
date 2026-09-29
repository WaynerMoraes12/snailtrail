#include <gtest/gtest.h>

#include "snailtrail/sql/fingerprint.hpp"

using namespace snailtrail::sql;

namespace {

std::string fp(std::string_view sql) { return fingerprint(sql).text; }

}

TEST(Fingerprint, ReplacesLiteralsAndLowercases) {
    EXPECT_EQ(fp("SELECT * FROM Orders WHERE id = 42 AND status = 'paid'"),
              "select * from orders where id = ? and status = ?");
}

TEST(Fingerprint, IgnoresFormattingAndComments) {
    const auto a = fingerprint("SELECT name\n  FROM users\n WHERE id=1 -- by pk");
    const auto b = fingerprint("select   name from `users` where id = 999 /* other */;");
    EXPECT_EQ(a.text, "select name from users where id = ?");
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.id, b.id);
}

TEST(Fingerprint, CollapsesInListsOfAnyLength) {
    EXPECT_EQ(fp("SELECT * FROM t WHERE id IN (1)"), "select * from t where id in(?+)");
    EXPECT_EQ(fp("SELECT * FROM t WHERE id IN (1, 2, 3, -4, NULL)"),
              "select * from t where id in(?+)");
    EXPECT_EQ(fp("SELECT * FROM t WHERE id NOT IN (?, ?)"),
              "select * from t where id not in(?+)");
}

TEST(Fingerprint, KeepsInSubqueries) {
    EXPECT_EQ(fp("SELECT * FROM t WHERE id IN (SELECT tid FROM u WHERE x = 5)"),
              "select * from t where id in (select tid from u where x = ?)");
}

TEST(Fingerprint, CollapsesMultiRowInserts) {
    const auto one = fingerprint("INSERT INTO t (a, b) VALUES (1, 'x')");
    const auto many = fingerprint("INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y'), (3, NOW())");
    EXPECT_EQ(one.text, "insert into t(a, b) values(?+)");
    EXPECT_EQ(one.id, many.id);
    EXPECT_EQ(one.kind, StatementKind::Insert);
}

TEST(Fingerprint, KeepsOnDuplicateKeyUpdate) {
    EXPECT_EQ(fp("INSERT INTO t (a) VALUES (1),(2) ON DUPLICATE KEY UPDATE a = VALUES(a) + 1"),
              "insert into t(a) values(?+) on duplicate key update a = values (a) + ?");
}

TEST(Fingerprint, FoldsSignsIntoNumbers) {
    EXPECT_EQ(fp("SELECT * FROM t WHERE x = -5 AND y > +3"),
              "select * from t where x = ? and y > ?");
    EXPECT_EQ(fp("SELECT a - 1 FROM t"), "select a - ? from t");
    EXPECT_EQ(fp("SELECT a FROM t LIMIT 10, 20"), "select a from t limit ?, ?");
}

TEST(Fingerprint, SpacingIsCanonical) {
    EXPECT_EQ(fp("SELECT COUNT( * ) , MAX(o.total)FROM orders o"),
              "select count(*), max(o.total) from orders o");
    EXPECT_EQ(fp("SELECT * FROM (SELECT 1) AS d WHERE (a = 1 OR b = 2)"),
              "select * from (select ?) as d where (a = ? or b = ?)");
}

TEST(Fingerprint, LiteralsInsideStringsCannotConfuseIt) {
    EXPECT_EQ(fp("SELECT * FROM t WHERE note = 'IN (1, 2) -- ? /* x */'"),
              "select * from t where note = ?");
}

TEST(Fingerprint, KeepsVariablesAndNull) {
    EXPECT_EQ(fp("SELECT @Total := 1, @@session.sql_mode FROM t WHERE a IS NULL"),
              "select @total := ?, @@session.sql_mode from t where a is null");
}

TEST(Fingerprint, ClassifiesStatements) {
    EXPECT_EQ(fingerprint("select 1").kind, StatementKind::Select);
    EXPECT_EQ(fingerprint("(SELECT 1) UNION (SELECT 2)").kind, StatementKind::Select);
    EXPECT_EQ(fingerprint("UPDATE t SET a = 1").kind, StatementKind::Update);
    EXPECT_EQ(fingerprint("DELETE FROM t").kind, StatementKind::Delete);
    EXPECT_EQ(fingerprint("REPLACE INTO t VALUES (1)").kind, StatementKind::Replace);
    EXPECT_EQ(fingerprint("CREATE TABLE t (a INT)").kind, StatementKind::Ddl);
    EXPECT_EQ(fingerprint("COMMIT").kind, StatementKind::Transaction);
    EXPECT_EQ(fingerprint("SHOW PROCESSLIST").kind, StatementKind::Utility);
    EXPECT_EQ(fingerprint("CALL refresh_stats(1)").kind, StatementKind::Call);
    EXPECT_EQ(fingerprint("WITH recent AS (SELECT * FROM o) DELETE FROM o").kind,
              StatementKind::Delete);
    EXPECT_EQ(fingerprint("WITH r AS (SELECT 1) SELECT * FROM r").kind, StatementKind::Select);
    EXPECT_EQ(fingerprint("hello world").kind, StatementKind::Unknown);
    EXPECT_EQ(statement_kind_name(StatementKind::Select), "SELECT");
}

TEST(Fingerprint, IdIsStableAndPrintable) {
    const auto f = fingerprint("SELECT 1");
    EXPECT_EQ(f.text, "select ?");
    EXPECT_EQ(f.id_hex().size(), 16U);
    EXPECT_EQ(f.id, fingerprint("select 2").id);
    EXPECT_NE(f.id, fingerprint("select 1 from dual").id);
}

TEST(Fingerprint, ReusesItsBuffers) {
    Fingerprinter fingerprinter;
    Fingerprint out;
    fingerprinter.compute_into("SELECT a FROM t WHERE b = 1", out);
    const std::string first = out.text;
    fingerprinter.compute_into("SELECT x", out);
    EXPECT_EQ(out.text, "select x");
    fingerprinter.compute_into("SELECT a FROM t WHERE b = 2", out);
    EXPECT_EQ(out.text, first);
}

TEST(Fingerprint, HandlesDegenerateInput) {
    EXPECT_EQ(fp(""), "");
    EXPECT_EQ(fp(";;;"), "");
    EXPECT_EQ(fp("SELECT (("), "select ((");
    EXPECT_EQ(fp("INSERT INTO t VALUES (1, 2"), "insert into t values(?+)");
}
