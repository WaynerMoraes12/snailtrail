#include <gtest/gtest.h>

#include "support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <system_error>

#include "snailtrail/analysis/analyzer.hpp"
#include "snailtrail/log/generator.hpp"

using namespace snailtrail;
using analysis::AnalyzeOptions;
using analysis::Analyzer;
using analysis::Report;

namespace {

const std::string& generated_log() {
    static const std::string text = [] {
        log::GeneratorOptions options;
        options.events = 6000;
        options.seed = 11;
        return log::SlowLogGenerator(options).generate();
    }();
    return text;
}

const schema::SchemaCatalog& shop() {
    static const schema::SchemaCatalog catalog = [] {
        return schema::SchemaCatalog::from_ddl(snailtrail::testing::read_sample("shop_schema.sql"));
    }();
    return catalog;
}

AnalyzeOptions options(unsigned threads) {
    AnalyzeOptions o;
    o.threads = threads;
    o.min_chunk_bytes = 16 * 1024;
    return o;
}

const analysis::ClassReport* find_class(const Report& r, std::string_view fragment) {
    for (const auto& c : r.classes) {
        if (c.stats.fingerprint().find(fragment) != std::string::npos) return &c;
    }
    return nullptr;
}

bool has_rule(const analysis::ClassReport& c, std::string_view id) {
    return std::any_of(c.findings.begin(), c.findings.end(), [&](const auto& f) { return f.rule_id == id; });
}

}

TEST(Analyzer, BuildsARankedReport) {
    const Analyzer analyzer(options(4), &shop());
    const Report r = analyzer.analyze_text(generated_log(), "generated.log");
    EXPECT_EQ(r.run.source, "generated.log");
    EXPECT_GT(r.run.chunks, 1U);
    EXPECT_GT(r.totals.events, 5000U);
    EXPECT_EQ(r.totals.events + r.run.skipped, 6000U);
    EXPECT_EQ(r.class_count, r.classes.size());
    EXPECT_GE(r.class_count, 18U);
    EXPECT_EQ(r.run.schema_tables, 6U);

    for (std::size_t i = 1; i < r.classes.size(); ++i) {
        EXPECT_GE(r.classes[i - 1].stats.query_time().sum(), r.classes[i].stats.query_time().sum());
        EXPECT_EQ(r.classes[i].rank, i + 1);
    }
    const double shares = std::accumulate(r.classes.begin(), r.classes.end(), 0.0,
                                          [](double sum, const auto& c) { return sum + c.time_share; });
    EXPECT_NEAR(shares, 1.0, 1e-9);
}

TEST(Analyzer, AdvisesEveryClassFromItsWorstSample) {
    const Analyzer analyzer(options(4), &shop());
    const Report r = analyzer.analyze_text(generated_log());

    const auto* items = find_class(r, "from order_items oi join products p");
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->tables, (std::vector<std::string>{"order_items", "products"}));
    EXPECT_EQ(items->label(), "SELECT order_items, products");
    EXPECT_TRUE(has_rule(*items, "ST001"));
    EXPECT_TRUE(has_rule(*items, "ST014"));

    const auto* phone = find_class(r, "where phone = ?");
    ASSERT_NE(phone, nullptr);
    EXPECT_TRUE(has_rule(*phone, "ST005"));

    const auto* orders = find_class(r, "from orders where customer_id = ? and status = ?");
    ASSERT_NE(orders, nullptr);
    EXPECT_TRUE(has_rule(*orders, "ST001"));

    const auto* point = find_class(r, "from sessions where token = ?");
    ASSERT_NE(point, nullptr);
    EXPECT_TRUE(point->findings.empty());

    const auto counts = r.findings_by_severity();
    EXPECT_GT(counts[2], 0U);
    EXPECT_GT(counts[1], 0U);
}

TEST(Analyzer, ResultsDoNotDependOnTheThreadCount) {
    const Analyzer one(options(1), &shop());
    const Analyzer many(options(8), &shop());
    const Report a = one.analyze_text(generated_log());
    const Report b = many.analyze_text(generated_log());
    EXPECT_EQ(a.run.chunks, 1U);
    EXPECT_GT(b.run.chunks, 1U);
    ASSERT_EQ(a.classes.size(), b.classes.size());
    for (std::size_t i = 0; i < a.classes.size(); ++i) {
        const auto& x = a.classes[i];
        const auto& y = b.classes[i];
        EXPECT_EQ(x.stats.id(), y.stats.id());
        EXPECT_EQ(x.stats.calls(), y.stats.calls());
        EXPECT_EQ(x.stats.query_time().sum(), y.stats.query_time().sum());
        EXPECT_EQ(x.stats.percentile_us(0.99), y.stats.percentile_us(0.99));
        EXPECT_EQ(x.stats.worst().sql, y.stats.worst().sql);
        ASSERT_EQ(x.findings.size(), y.findings.size());
        for (std::size_t j = 0; j < x.findings.size(); ++j) {
            EXPECT_EQ(x.findings[j].title, y.findings[j].title);
            EXPECT_EQ(x.findings[j].detail, y.findings[j].detail);
        }
    }
}

TEST(Analyzer, HonoursTopSortSeverityAndAdviceOptions) {
    AnalyzeOptions o = options(2);
    o.top = 5;
    o.sort = analysis::SortKey::Calls;
    o.min_severity = advisor::Severity::Critical;
    const Report r = Analyzer(o, &shop()).analyze_text(generated_log());
    ASSERT_EQ(r.classes.size(), 5U);
    EXPECT_GT(r.class_count, 5U);
    EXPECT_EQ(r.sort, analysis::SortKey::Calls);
    for (std::size_t i = 1; i < r.classes.size(); ++i) {
        EXPECT_GE(r.classes[i - 1].stats.calls(), r.classes[i].stats.calls());
    }
    for (const auto& c : r.classes) {
        for (const auto& f : c.findings) EXPECT_EQ(f.severity, advisor::Severity::Critical);
    }

    AnalyzeOptions quiet = options(2);
    quiet.advise = false;
    const Report q = Analyzer(quiet).analyze_text(generated_log());
    EXPECT_TRUE(std::all_of(q.classes.begin(), q.classes.end(), [](const auto& c) { return c.findings.empty(); }));
    EXPECT_FALSE(q.classes.front().tables.empty());
}

TEST(Analyzer, FiltersByDatabase) {
    AnalyzeOptions o = options(2);
    o.database = "analytics";
    const Report r = Analyzer(o).analyze_text(generated_log());
    EXPECT_EQ(r.totals.events, 0U);
    EXPECT_GT(r.run.filtered, 5000U);
    EXPECT_TRUE(r.classes.empty());

    o.database = "SHOP";
    EXPECT_GT(Analyzer(o).analyze_text(generated_log()).totals.events, 5000U);
}

TEST(Analyzer, AttributesDatabasesAcrossChunkBoundaries) {
    std::string log;
    int db = 0;
    for (int i = 0; i < 3000; ++i) {
        log += "# Time: 2024-01-15T10:00:00.000000Z\n# User@Host: app[app] @ h []  Id: 1\n";
        log += "# Query_time: 0.001000  Lock_time: 0.000000 Rows_sent: 1  Rows_examined: 1\n";
        if (i % 150 == 0) log += "use db" + std::to_string(db++ % 4) + ";\n";
        log += "SET timestamp=1705312800;\nSELECT * FROM t" + std::to_string(i % 3) + " WHERE id = " +
               std::to_string(i) + ";\n";
    }
    AnalyzeOptions o = options(1);
    o.min_chunk_bytes = 4 * 1024;
    const Report sequential = Analyzer(o).analyze_text(log);
    o.threads = 8;
    const Report parallel = Analyzer(o).analyze_text(log);
    EXPECT_EQ(sequential.run.chunks, 1U);
    EXPECT_GT(parallel.run.chunks, 4U);
    ASSERT_EQ(sequential.classes.size(), 3U);
    ASSERT_EQ(parallel.classes.size(), 3U);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(sequential.classes[i].stats.databases(), parallel.classes[i].stats.databases());
        EXPECT_EQ(sequential.classes[i].stats.worst().database, parallel.classes[i].stats.worst().database);
        EXPECT_EQ(sequential.classes[i].stats.databases().size(), 4U);
    }

    o.database = "db2";
    const Report filtered = Analyzer(o).analyze_text(log);
    o.threads = 1;
    const Report filtered_sequential = Analyzer(o).analyze_text(log);
    EXPECT_EQ(filtered.totals.events, filtered_sequential.totals.events);
    EXPECT_EQ(filtered.totals.events, 750U);
    EXPECT_EQ(filtered.run.filtered, 2250U);
}

TEST(Analyzer, FilesStreamsAndTextAgree) {
    const auto path = std::filesystem::temp_directory_path() / "snailtrail-analyzer-test.log";
    {
        std::ofstream out(path, std::ios::binary);
        out << generated_log();
    }
    const Analyzer analyzer(options(4), &shop());
    const Report from_file = analyzer.analyze_file(path);
    std::istringstream stream(generated_log());
    const Report from_stream = analyzer.analyze_stream(stream);
    const Report from_text = analyzer.analyze_text(generated_log());
    std::filesystem::remove(path);

    EXPECT_EQ(from_file.run.bytes, generated_log().size());
    EXPECT_EQ(from_file.totals.events, from_text.totals.events);
    EXPECT_EQ(from_stream.totals.events, from_text.totals.events);
    EXPECT_EQ(from_stream.totals.query_time_us, from_text.totals.query_time_us);
    EXPECT_EQ(from_stream.class_count, from_text.class_count);
    EXPECT_EQ(from_stream.run.source, "<stdin>");
    EXPECT_THROW(static_cast<void>(analyzer.analyze_file("/no/such/slow.log")), std::system_error);
}

TEST(Analyzer, RanksTheMissingIndexFirstInARealMySqlLog) {
    AnalyzeOptions o = options(4);
    o.database = "shop";
    const Report r = Analyzer(o, &shop()).analyze_text(snailtrail::testing::read_sample("mysql-8.4-slow.log"));
    ASSERT_FALSE(r.classes.empty());
    const auto& top = r.classes.front();
    EXPECT_EQ(top.label(), "SELECT order_items, products");
    EXPECT_GT(top.time_share, 0.5);
    ASSERT_TRUE(has_rule(top, "ST001"));
    const auto fix = std::find_if(top.findings.begin(), top.findings.end(),
                                  [](const auto& f) { return f.rule_id == "ST001"; });
    EXPECT_NE(fix->suggestion.find("ADD INDEX idx_order_items_order_id (order_id)"), std::string::npos);
    EXPECT_EQ(fix->severity, advisor::Severity::Critical);

    const auto* phone = find_class(r, "where phone = ?");
    ASSERT_NE(phone, nullptr);
    EXPECT_TRUE(has_rule(*phone, "ST005"));
    const auto* transactions = find_class(r, "commit");
    ASSERT_NE(transactions, nullptr);
    EXPECT_EQ(transactions->stats.kind(), sql::StatementKind::Transaction);
    EXPECT_TRUE(transactions->findings.empty());
}

TEST(Analyzer, HandlesEmptyInput) {
    const Report r = Analyzer().analyze_text("");
    EXPECT_EQ(r.totals.events, 0U);
    EXPECT_TRUE(r.classes.empty());
    EXPECT_GE(Analyzer().effective_threads(), 1U);
}

TEST(SortKey, RoundTripsNames) {
    for (auto k : {analysis::SortKey::TotalTime, analysis::SortKey::Calls, analysis::SortKey::AverageTime,
                   analysis::SortKey::P95, analysis::SortKey::MaxTime, analysis::SortKey::RowsExamined}) {
        EXPECT_EQ(analysis::parse_sort_key(analysis::sort_key_name(k)), k);
    }
    EXPECT_FALSE(analysis::parse_sort_key("speed").has_value());
}
