#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "snailtrail/schema/catalog.hpp"

using namespace snailtrail::schema;
using snailtrail::sql::IndexKind;

namespace {

std::string read_sample(const std::string& name) {
    std::ifstream in(std::string(SNAILTRAIL_SAMPLES_DIR) + "/" + name, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}

TEST(Schema, CategorizesMySqlTypes) {
    EXPECT_EQ(categorize_type("bigint unsigned"), TypeCategory::Integer);
    EXPECT_EQ(categorize_type("TINYINT(1)"), TypeCategory::Integer);
    EXPECT_EQ(categorize_type("decimal(10,2)"), TypeCategory::Decimal);
    EXPECT_EQ(categorize_type("double"), TypeCategory::Float);
    EXPECT_EQ(categorize_type("varchar(20)"), TypeCategory::String);
    EXPECT_EQ(categorize_type("enum('a','b')"), TypeCategory::String);
    EXPECT_EQ(categorize_type("varbinary(16)"), TypeCategory::Binary);
    EXPECT_EQ(categorize_type("datetime"), TypeCategory::Temporal);
    EXPECT_EQ(categorize_type("json"), TypeCategory::Json);
    EXPECT_EQ(categorize_type("geometry"), TypeCategory::Other);
    EXPECT_TRUE(is_numeric(TypeCategory::Decimal));
    EXPECT_TRUE(is_textual(TypeCategory::Binary));
    EXPECT_EQ(type_category_name(TypeCategory::String), "string");
}

TEST(Schema, LoadsAMysqldumpFile) {
    std::vector<std::string> warnings;
    const auto catalog = SchemaCatalog::from_ddl(read_sample("shop_schema.sql"), &warnings);
    EXPECT_TRUE(warnings.empty());
    ASSERT_EQ(catalog.size(), 6U);

    const Table* orders = catalog.find_table("ORDERS");
    ASSERT_NE(orders, nullptr);
    EXPECT_EQ(orders->columns().size(), 7U);
    ASSERT_NE(orders->find_column("Status"), nullptr);
    EXPECT_EQ(orders->find_column("status")->category, TypeCategory::String);
    EXPECT_FALSE(orders->find_column("customer_id")->nullable);
    EXPECT_TRUE(orders->find_column("coupon_code")->nullable);
    ASSERT_NE(orders->primary_key(), nullptr);
    EXPECT_EQ(orders->primary_key()->columns, (std::vector<std::string>{"id"}));
    EXPECT_NE(orders->find_index("idx_orders_customer"), nullptr);

    const Table* customers = catalog.find_table("customers");
    EXPECT_EQ(customers->find_column("phone")->category, TypeCategory::String);
    EXPECT_EQ(customers->find_index("uq_customers_email")->kind, IndexKind::Unique);
    EXPECT_EQ(catalog.find_table("nope"), nullptr);
}

TEST(Schema, FindsTablesByColumn) {
    const auto catalog = SchemaCatalog::from_ddl(read_sample("shop_schema.sql"));
    const auto with_customer = catalog.tables_with_column("customer_id");
    ASSERT_EQ(with_customer.size(), 3U);
    EXPECT_EQ(with_customer[0]->name(), "orders");
    EXPECT_EQ(catalog.tables_with_column("expires_at").size(), 1U);
}

TEST(Schema, IndexServesEqualityInAnyOrderThenTailInOrder) {
    const Index idx{"idx", IndexKind::Regular, {"customer_id", "status", "created_at"}};
    const std::vector<std::string> eq = {"status", "customer_id"};
    const std::vector<std::string> tail = {"created_at"};
    const std::vector<std::string> none;
    EXPECT_TRUE(idx.serves(eq, tail));
    EXPECT_TRUE(idx.serves(eq, none));
    EXPECT_TRUE(idx.serves(std::vector<std::string>{"CUSTOMER_ID"}, none));
    EXPECT_FALSE(idx.serves(std::vector<std::string>{"status"}, none));
    EXPECT_FALSE(idx.serves(std::vector<std::string>{"customer_id"}, tail));

    const Index fulltext{"ft", IndexKind::Fulltext, {"customer_id"}};
    EXPECT_FALSE(fulltext.serves(std::vector<std::string>{"customer_id"}, none));
}

TEST(Schema, DetectsIndexesAWiderIndexMakesRedundant) {
    Table t("orders");
    t.add_index({"PRIMARY", IndexKind::Primary, {"customer_id"}});
    t.add_index({"idx_customer", IndexKind::Regular, {"customer_id"}});
    t.add_index({"idx_status", IndexKind::Regular, {"status"}});
    t.add_index({"uq_customer_status", IndexKind::Unique, {"customer_id", "status"}});

    const std::vector<std::string> eq = {"customer_id", "status"};
    const std::vector<std::string> tail = {"created_at"};
    const auto redundant = t.indexes_made_redundant_by(eq, tail);
    ASSERT_EQ(redundant.size(), 2U);
    EXPECT_EQ(redundant[0]->name, "idx_customer");
    EXPECT_EQ(redundant[1]->name, "idx_status");
    EXPECT_EQ(t.index_serving(eq, {}), t.find_index("uq_customer_status"));
    EXPECT_EQ(t.index_serving(eq, tail), nullptr);
}

TEST(Schema, AppliesAlterTableAndCreateIndex) {
    const auto catalog = SchemaCatalog::from_ddl(R"(
CREATE TABLE t (id INT PRIMARY KEY, a INT, b VARCHAR(10), KEY idx_a (a));
ALTER TABLE t ADD INDEX idx_ab (a, b), DROP INDEX idx_a, ADD COLUMN c DATE;
CREATE INDEX idx_c ON t (c);
ALTER TABLE t ADD KEY (b);
CREATE INDEX idx_x ON unknown_table (x);
)");
    const Table* t = catalog.find_table("t");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->find_index("idx_a"), nullptr);
    ASSERT_NE(t->find_index("idx_ab"), nullptr);
    EXPECT_NE(t->find_index("idx_c"), nullptr);
    EXPECT_NE(t->find_index("b"), nullptr);
    ASSERT_NE(t->find_column("c"), nullptr);
    EXPECT_EQ(t->find_column("c")->category, TypeCategory::Temporal);
    EXPECT_NE(catalog.find_table("unknown_table"), nullptr);
}

TEST(Schema, ReportsSkippedStatements) {
    std::vector<std::string> warnings;
    const auto catalog = SchemaCatalog::from_ddl(
        "CREATE TABLE ok (id INT);\nCREATE TABLE broken (id INT,;\nCREATE TABLE ok2 (id INT);",
        &warnings);
    EXPECT_EQ(catalog.size(), 2U);
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings[0].find("skipped"), std::string::npos);
}
