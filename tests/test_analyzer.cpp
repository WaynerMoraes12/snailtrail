#include <gtest/gtest.h>

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
        std::ifstream in(std::string(SNAILTRAIL_SAMPLES_DIR) + "/shop_schema.sql", std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return schema::SchemaCatalog::from_ddl(s.str());
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
