#include <gtest/gtest.h>

#include "support.hpp"

#include <algorithm>
#include <optional>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "snailtrail/advisor/index_advisor.hpp"
#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/sql/fingerprint.hpp"
#include "snailtrail/sql/parser.hpp"

using namespace snailtrail;
using advisor::Finding;
using advisor::Severity;

namespace {

using snailtrail::testing::read_sample;

const schema::SchemaCatalog& shop() {
    static const schema::SchemaCatalog catalog = schema::SchemaCatalog::from_ddl(read_sample("shop_schema.sql"));
    return catalog;
}

const advisor::RuleEngine& engine() {
    static const advisor::RuleEngine e = advisor::RuleEngine::with_default_rules();
    return e;
}

std::vector<Finding> advise(std::string_view sql, const schema::SchemaCatalog* catalog = &shop(),
                            const stats::QueryClass* stats = nullptr) {
    return engine().advise(sql, catalog, stats);
}

std::optional<Finding> rule(const std::vector<Finding>& findings, std::string_view id) {
    const auto it = std::find_if(findings.begin(), findings.end(),
                                 [&](const Finding& f) { return f.rule_id == id; });
    if (it == findings.end()) return std::nullopt;
    return *it;
}

std::size_t count(const std::vector<Finding>& findings, std::string_view id) {
    return static_cast<std::size_t>(std::count_if(findings.begin(), findings.end(),
                                                  [&](const Finding& f) { return f.rule_id == id; }));
}

advisor::QueryFacts facts(std::string_view sql, const schema::SchemaCatalog* catalog = &shop()) {
    const auto statement = sql::parse(sql);
    return advisor::collect_facts(*statement, catalog);
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

stats::QueryClass metrics(std::string_view sql, std::uint64_t examined, std::uint64_t sent,
                          std::uint8_t flags = 0, int calls = 10) {
    const auto fp = sql::fingerprint(sql);
    stats::QueryClass c(fp.id, fp.text, fp.kind);
    for (int i = 0; i < calls; ++i) {
        log::QueryEvent e;
        e.sql = sql;
        e.query_time_us = 200'000;
        e.rows_examined = examined;
        e.rows_sent = sent;
        e.flags = flags;
        c.add(e);
    }
    return c;
}

}

TEST(QueryFacts, ResolvesAliasesAndClassifiesPredicates) {
    const auto f = facts("SELECT o.id FROM orders o JOIN customers c ON c.id = o.customer_id "
                         "WHERE c.country = 'BR' AND o.status IN ('paid', 'sent') AND o.total > 10 "
                         "ORDER BY o.created_at DESC LIMIT 10");
    EXPECT_EQ(f.kind, sql::StatementKind::Select);
    ASSERT_EQ(f.tables.size(), 2U);
    ASSERT_EQ(f.predicates.size(), 4U);
    const auto& country = f.predicates[0];
    EXPECT_EQ(country.kind, advisor::PredicateKind::Equality);
    EXPECT_EQ(country.column.table, "customers");
    EXPECT_EQ(country.column.ref, "c");
    EXPECT_EQ(country.value, advisor::ValueKind::String);
    EXPECT_EQ(f.predicates[1].kind, advisor::PredicateKind::In);
    EXPECT_EQ(f.predicates[1].in_list_size, 2U);
    EXPECT_EQ(f.predicates[2].kind, advisor::PredicateKind::Range);
    EXPECT_TRUE(f.predicates[3].is_join());
    EXPECT_TRUE(f.predicates[3].from_join);
    ASSERT_EQ(f.links.size(), 1U);
    ASSERT_EQ(f.order_by.size(), 1U);
    EXPECT_EQ(f.order_by[0].table, "orders");
    EXPECT_TRUE(f.order_by_descending);
    EXPECT_EQ(f.limit, 10U);
}

TEST(QueryFacts, UsesTheSchemaForUnqualifiedColumns) {
    const auto with = facts("SELECT * FROM orders JOIN customers ON customers.id = orders.customer_id "
                            "WHERE country = 'BR'");
    EXPECT_EQ(with.predicates[0].column.table, "customers");
    const auto without = facts("SELECT * FROM orders JOIN customers ON customers.id = orders.customer_id "
                               "WHERE country = 'BR'",
                               nullptr);
    EXPECT_FALSE(without.predicates[0].column.resolved());
}

TEST(QueryFacts, TracksOrNegationAndWrappers) {
    const auto f = facts("SELECT id FROM customers WHERE (email = 'a' OR phone = 'b') "
                         "AND NOT tier = 3 AND LOWER(city) = 'x' AND created_at IS NOT NULL");
    ASSERT_EQ(f.or_groups.size(), 1U);
    EXPECT_EQ(f.or_groups[0].columns.size(), 2U);
    EXPECT_TRUE(f.predicates[0].under_or);
    EXPECT_TRUE(f.predicates[2].negated);
    EXPECT_EQ(f.predicates[3].wrapper, "lower");
    EXPECT_EQ(f.predicates[3].expression_sql, "LOWER(city)");
    EXPECT_EQ(f.predicates[4].kind, advisor::PredicateKind::Range);
}

TEST(MissingIndex, SuggestsEqualityThenSortAndDropsTheRedundantIndex) {
    const auto findings = advise("SELECT id, total FROM orders WHERE customer_id = 812 AND status = 'paid' "
                                 "ORDER BY created_at DESC LIMIT 20");
    const auto f = rule(findings, "ST001");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->title, "orders needs an index on (customer_id, status, created_at)");
    EXPECT_TRUE(contains(f->suggestion, "ALTER TABLE orders ADD INDEX idx_orders_customer_id_status_created_at "
                                        "(customer_id, status, created_at);"));
    EXPECT_TRUE(contains(f->suggestion, "DROP INDEX idx_orders_customer"));
    EXPECT_TRUE(contains(f->detail, "idx_orders_customer covers only customer_id"));
    EXPECT_TRUE(contains(f->detail, "then sorts them (filesort)"));

    auto catalog = schema::SchemaCatalog::from_ddl(
        read_sample("shop_schema.sql") + "\nALTER TABLE orders ADD INDEX i (status, customer_id);");
    const auto partial = rule(advise("SELECT id FROM orders WHERE customer_id = 1 AND status = 'paid' "
                                     "ORDER BY created_at DESC LIMIT 20",
                                     &catalog),
                              "ST001");
    ASSERT_TRUE(partial);
    EXPECT_TRUE(contains(partial->detail, "i serves the lookup but not the sort"));
}

TEST(MissingIndex, StaysQuietWhenAnIndexAlreadyServes) {
    auto catalog = schema::SchemaCatalog::from_ddl(
        read_sample("shop_schema.sql") + "\nALTER TABLE orders ADD INDEX i (status, customer_id, created_at);");
    const auto findings = advise("SELECT id FROM orders WHERE customer_id = 1 AND status = 'paid' "
                                 "ORDER BY created_at DESC LIMIT 20",
                                 &catalog);
    EXPECT_FALSE(rule(findings, "ST001"));
    EXPECT_FALSE(rule(advise("SELECT * FROM products WHERE id = 5", nullptr), "ST001"));
    EXPECT_FALSE(rule(advise("SELECT * FROM customers WHERE email = 'x'"), "ST001"));
}

TEST(MissingIndex, IndexesTheJoinColumnOfTheDrivenTable) {
    const auto findings = advise("SELECT oi.product_id, p.name FROM order_items oi "
                                 "JOIN products p ON p.id = oi.product_id WHERE oi.order_id = 5");
    ASSERT_EQ(count(findings, "ST001"), 1U);
    EXPECT_TRUE(contains(rule(findings, "ST001")->suggestion, "ADD INDEX idx_order_items_order_id (order_id)"));
}

TEST(MissingIndex, HandlesRangesPrefixLikeAndWrites) {
    const auto d = rule(advise("DELETE FROM sessions WHERE expires_at < '2024-01-01'"), "ST001");
    ASSERT_TRUE(d);
    EXPECT_TRUE(contains(d->suggestion, "(expires_at)"));

    const auto like = rule(advise("SELECT id FROM products WHERE category = 'x' AND name LIKE 'abc%'"), "ST001");
    ASSERT_TRUE(like);
    EXPECT_TRUE(contains(like->suggestion, "(category, name)"));

    EXPECT_FALSE(rule(advise("SELECT id FROM customers WHERE email = 'a' OR phone = 'b'"), "ST001"));
    EXPECT_FALSE(rule(advise("SELECT id FROM customers WHERE phone = 551199"), "ST001"));
}

TEST(MissingIndex, SaysWhenTheSchemaWasNotChecked) {
    const auto f = rule(advise("SELECT id FROM orders WHERE status = 'paid'", nullptr), "ST001");
    ASSERT_TRUE(f);
    EXPECT_TRUE(contains(f->detail, "pass the schema"));
}

TEST(MissingIndex, BecomesCriticalWithSlowLogEvidence) {
    const std::string sql = "SELECT id FROM orders WHERE status = 'paid'";
    const auto stats = metrics(sql, 2'000'000, 30, static_cast<std::uint8_t>(log::ExecutionFlag::FullScan));
    const auto f = rule(advise(sql, &shop(), &stats), "ST001");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->severity, Severity::Critical);
    EXPECT_TRUE(contains(f->detail, "100% of executions scanned a table"));
}

TEST(UnboundedWrite, FlagsUpdatesAndDeletesWithoutWhere) {
    const auto u = rule(advise("UPDATE customers SET tier = 1"), "ST002");
    ASSERT_TRUE(u);
    EXPECT_EQ(u->severity, Severity::Critical);
    EXPECT_EQ(u->title, "UPDATE without WHERE rewrites every row of customers");
    const auto d = rule(advise("DELETE FROM sessions LIMIT 1000"), "ST002");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->severity, Severity::Warning);
    EXPECT_FALSE(rule(advise("DELETE FROM sessions WHERE id = 1"), "ST002"));
}

TEST(CartesianJoin, FindsTablesWithNoJoinCondition) {
    const auto f = rule(advise("SELECT COUNT(*) FROM customers c, orders o "
                                   "WHERE c.country = 'BR' AND o.status = 'paid'"),
                            "ST003");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->severity, Severity::Critical);
    EXPECT_EQ(f->title, "customers × orders: no join condition (cartesian product)");
    EXPECT_TRUE(contains(f->suggestion, "o.customer_id = c.id"));

    EXPECT_FALSE(rule(advise("SELECT * FROM customers c, orders o WHERE o.customer_id = c.id"), "ST003"));
    EXPECT_FALSE(rule(advise("SELECT * FROM a CROSS JOIN b", nullptr), "ST003"));
    EXPECT_FALSE(rule(advise("SELECT * FROM a JOIN b USING (id)", nullptr), "ST003"));
    EXPECT_TRUE(rule(advise("SELECT * FROM a JOIN b ON b.flag = 1", nullptr), "ST003"));
    EXPECT_FALSE(rule(advise("SELECT * FROM a, b WHERE x = y", nullptr), "ST003"));
}

TEST(NonSargable, ExplainsFunctionsOnColumns) {
    const auto lower = rule(advise("SELECT id FROM customers WHERE LOWER(email) = 'x@y.com'"), "ST004");
    ASSERT_TRUE(lower);
    EXPECT_EQ(lower->title, "LOWER(email) hides customers.email from its index");
    EXPECT_TRUE(contains(lower->suggestion, "drop LOWER()"));
    EXPECT_TRUE(contains(lower->suggestion, "already has one"));

    const auto date = rule(advise("SELECT id FROM orders WHERE DATE(created_at) = '2024-01-15'"), "ST004");
    ASSERT_TRUE(date);
    EXPECT_TRUE(contains(date->suggestion,
                         "created_at >= '2024-01-15' AND created_at < '2024-01-15' + INTERVAL 1 DAY"));

    const auto year = rule(advise("SELECT id FROM orders WHERE YEAR(created_at) = 2024"), "ST004");
    ASSERT_TRUE(year);
    EXPECT_TRUE(contains(year->suggestion, "created_at >= '2024-01-01' AND created_at < '2025-01-01'"));

    const auto math = rule(advise("SELECT id FROM products WHERE price * 1.1 > 100"), "ST004");
    ASSERT_TRUE(math);
    EXPECT_TRUE(contains(math->suggestion, "Move the arithmetic"));

    EXPECT_FALSE(rule(advise("SELECT id FROM orders WHERE created_at >= '2024-01-15'"), "ST004"));
}

TEST(ImplicitConversion, NeedsTheSchemaAndQuotesTheValue) {
    const std::string sql = "SELECT id, full_name FROM customers WHERE phone = 5511912345678";
    const auto f = rule(advise(sql), "ST005");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->title, "customers.phone is VARCHAR(20) but is compared to a number");
    EXPECT_TRUE(contains(f->suggestion, "phone = '5511912345678'"));
    EXPECT_TRUE(contains(f->suggestion, "ADD INDEX idx_customers_phone (phone)"));
    EXPECT_FALSE(rule(advise(sql, nullptr), "ST005"));
    EXPECT_FALSE(rule(advise("SELECT id FROM customers WHERE phone = '551199'"), "ST005"));

    const auto catalog = schema::SchemaCatalog::from_ddl("CREATE TABLE a (code VARCHAR(10)); CREATE TABLE b (id INT);");
    const auto join = rule(advise("SELECT * FROM a JOIN b ON a.code = b.id", &catalog), "ST005");
    ASSERT_TRUE(join);
    EXPECT_EQ(join->title, "a.code (VARCHAR(10)) is joined to b.id (INT)");
}

TEST(LeadingWildcard, SuggestsFulltextOrReversal) {
    const auto f = rule(advise("SELECT id FROM products WHERE name LIKE '%phone%'"), "ST006");
    ASSERT_TRUE(f);
    EXPECT_TRUE(contains(f->suggestion, "ADD FULLTEXT INDEX ft_products_name (name)"));
    EXPECT_TRUE(contains(f->suggestion, "AGAINST('phone')"));
    const auto suffix = rule(advise("SELECT id FROM customers WHERE email LIKE '%@example.com'"), "ST006");
    ASSERT_TRUE(suffix);
    EXPECT_TRUE(contains(suffix->suggestion, "REVERSE(email)"));
    EXPECT_FALSE(rule(advise("SELECT id FROM products WHERE name LIKE 'phone%'"), "ST006"));
}

TEST(NotInSubquery, RewritesAsNotExists) {
    const auto f = rule(advise("SELECT id, email FROM customers WHERE id NOT IN (SELECT customer_id FROM orders)"),
                            "ST007");
    ASSERT_TRUE(f);
    EXPECT_TRUE(contains(f->suggestion,
                         "NOT EXISTS (SELECT 1 FROM orders WHERE orders.customer_id = customers.id)"));
    EXPECT_TRUE(rule(advise("SELECT id FROM customers WHERE NOT (id IN (SELECT customer_id FROM orders))"), "ST007"));
    EXPECT_FALSE(rule(advise("SELECT id FROM customers WHERE id IN (SELECT customer_id FROM orders)"), "ST007"));
    EXPECT_FALSE(rule(advise("SELECT id FROM customers WHERE id NOT IN (1, 2)"), "ST007"));
}

TEST(DeepPagination, SuggestsKeysetPagination) {
    const auto f = rule(advise("SELECT * FROM reviews WHERE product_id = 9 ORDER BY created_at DESC LIMIT 12000, 20"),
                            "ST008");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->title, "LIMIT 12000, 20 reads and discards 12,000 rows");
    EXPECT_TRUE(contains(f->suggestion, "WHERE created_at < ? ORDER BY created_at DESC LIMIT 20"));
    EXPECT_FALSE(rule(advise("SELECT * FROM reviews LIMIT 20 OFFSET 100"), "ST008"));
}

TEST(SimpleRules, OrderByRandLargeInListHavingAndStar) {
    const auto rand = rule(advise("SELECT id, name FROM products ORDER BY RAND() LIMIT 8"), "ST009");
    ASSERT_TRUE(rand);
    EXPECT_EQ(rand->title, "ORDER BY RAND() sorts every row to return 8");

    std::string in = "SELECT id FROM products WHERE id IN (";
    for (int i = 0; i < 250; ++i) in += (i > 0 ? ", " : "") + std::to_string(i);
    EXPECT_TRUE(rule(advise(in + ")"), "ST011"));
    EXPECT_FALSE(rule(advise("SELECT id FROM products WHERE id IN (1, 2, 3)"), "ST011"));

    const auto having =
        rule(advise("SELECT category, COUNT(*) FROM products GROUP BY category HAVING category <> 'books'"), "ST012");
    ASSERT_TRUE(having);
    EXPECT_TRUE(contains(having->suggestion, "WHERE category <> 'books'"));
    EXPECT_FALSE(rule(advise("SELECT category FROM products GROUP BY category HAVING COUNT(*) > 5"), "ST012"));

    const auto star = rule(advise("SELECT * FROM products WHERE id = 1"), "ST013");
    ASSERT_TRUE(star);
    EXPECT_TRUE(contains(star->detail, "description (text)"));
    const auto qualified =
        rule(advise("SELECT p.* FROM products p JOIN reviews r ON r.product_id = p.id WHERE r.id = 1"), "ST013");
    ASSERT_TRUE(qualified);
    EXPECT_EQ(qualified->title, "SELECT * reads every column of products");
    EXPECT_FALSE(rule(advise("SELECT COUNT(*) FROM products"), "ST013"));
}

TEST(OrAcrossColumns, PointsAtTheUnindexedColumn) {
    const auto f = rule(advise("SELECT id FROM customers WHERE email = 'a@b.c' OR phone = '1'"), "ST010");
    ASSERT_TRUE(f);
    EXPECT_EQ(f->title, "OR across email and phone");
    EXPECT_EQ(f->severity, Severity::Warning);
    EXPECT_TRUE(contains(f->detail, "phone has no index"));
    EXPECT_FALSE(rule(advise("SELECT id FROM orders WHERE status = 'a' OR status = 'b'"), "ST010"));
}

TEST(MetricRules, UseTheSlowLogEvidence) {
    const std::string sql = "SELECT oi.product_id FROM order_items oi WHERE oi.order_id = 5";
    const auto heavy = metrics(sql, 480'000, 3, static_cast<std::uint8_t>(log::ExecutionFlag::TmpTableOnDisk));
    const auto findings = advise(sql, &shop(), &heavy);
    const auto ratio = rule(findings, "ST014");
    ASSERT_TRUE(ratio);
    EXPECT_EQ(ratio->severity, Severity::Critical);
    EXPECT_EQ(ratio->title, "Examines 480k rows to return 3 (160k:1)");
    const auto disk = rule(findings, "ST015");
    ASSERT_TRUE(disk);
    EXPECT_EQ(disk->title, "Temporary tables spill to disk in 100% of executions");

    const auto light = metrics(sql, 10, 3);
    EXPECT_FALSE(rule(advise(sql, &shop(), &light), "ST014"));
    EXPECT_FALSE(rule(advise(sql), "ST014"));
}

TEST(RuleEngine, ReportsUnparsedDmlButNotUtilityStatements) {
    const auto findings = advise("SELECT * FROM t WHERE a SOUNDS LIKE 'x'", nullptr);
    ASSERT_TRUE(rule(findings, "ST000"));
    EXPECT_TRUE(advise("SET @x = (", nullptr).empty());
    EXPECT_TRUE(advise("INSERT INTO t VALUES (1)", nullptr).empty());
}

TEST(RuleEngine, OrdersBySeverityAndCanDisableRules) {
    auto e = advisor::RuleEngine::with_default_rules();
    EXPECT_EQ(e.rules().size(), 15U);
    const auto findings = e.advise("SELECT * FROM customers c, orders o", &shop());
    ASSERT_GE(findings.size(), 2U);
    EXPECT_EQ(findings.front().severity, Severity::Critical);
    EXPECT_TRUE(std::is_sorted(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        return a.severity > b.severity;
    }));

    EXPECT_TRUE(e.disable("select-star"));
    EXPECT_TRUE(e.disable("ST003"));
    EXPECT_FALSE(e.disable("no-such-rule"));
    EXPECT_FALSE(e.is_enabled("ST013"));
    EXPECT_FALSE(e.is_enabled("cartesian-join"));
    EXPECT_TRUE(e.is_enabled("ST001"));
    EXPECT_TRUE(e.advise("SELECT * FROM customers c, orders o", &shop()).empty());
    ASSERT_NE(e.find("st005"), nullptr);
    EXPECT_EQ(e.find("st005")->info().name, "implicit-conversion");
    EXPECT_TRUE(e.find("implicit-conversion")->info().needs_schema);
}

TEST(IndexAdvisor, NamesIndexesWithinMySqlLimits) {
    EXPECT_EQ(advisor::index_name("Orders", {"customer_id", "created_at"}), "idx_orders_customer_id_created_at");
    EXPECT_EQ(advisor::index_name("t", {std::string(80, 'x')}).size(), 64U);
}
