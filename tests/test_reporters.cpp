#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>

#include "snailtrail/analysis/analyzer.hpp"
#include "snailtrail/log/generator.hpp"
#include "snailtrail/report/reporter.hpp"
#include "snailtrail/util/strings.hpp"

using namespace snailtrail;

namespace {

const analysis::Report& report() {
    static const analysis::Report r = [] {
        log::GeneratorOptions g;
        g.events = 3000;
        g.seed = 5;
        analysis::AnalyzeOptions o;
        o.threads = 2;
        o.min_chunk_bytes = 16 * 1024;
        return analysis::Analyzer(o).analyze_text(log::SlowLogGenerator(g).generate(), "demo.log");
    }();
    return r;
}

std::string render(std::string_view format, report::RenderOptions options = {}) {
    std::ostringstream out;
    report::make_reporter(format, options)->render(report(), out);
    return out.str();
}

std::size_t visible(std::string_view line) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\x1b') {
            while (i < line.size() && line[i] != 'm') ++i;
            continue;
        }
        if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) ++width;
    }
    return width;
}

}

TEST(TextReporter, RendersSummaryTableAndDetails) {
    const std::string text = render("text");
    EXPECT_TRUE(text.starts_with("SnailTrail " SNAILTRAIL_VERSION " · demo.log"));
    EXPECT_NE(text.find("Query time"), std::string::npos);
    EXPECT_NE(text.find("Query ID"), std::string::npos);
    EXPECT_NE(text.find("── #1 · "), std::string::npos);
    EXPECT_NE(text.find("Fingerprint"), std::string::npos);
    EXPECT_NE(text.find("Worst sample"), std::string::npos);
    EXPECT_NE(text.find("ST001"), std::string::npos);
    EXPECT_NE(text.find("→ ALTER TABLE"), std::string::npos);
    EXPECT_NE(text.find("events/s"), std::string::npos);
    EXPECT_EQ(text.find('\x1b'), std::string::npos);
}

TEST(TextReporter, UsesColorOnlyWhenAsked) {
    report::RenderOptions o;
    o.color = true;
    EXPECT_NE(render("text", o).find("\x1b[1;31m"), std::string::npos);
}

TEST(TextReporter, KeepsTableRowsWithinTheWidth) {
    report::RenderOptions o;
    o.color = true;
    o.width = 100;
    o.detail_limit = 0;
    const std::string text = render("text", o);
    for (std::string_view line : util::split(text, '\n')) {
        EXPECT_LE(visible(line), 100U) << line;
    }
}

TEST(JsonReporter, WritesTheStableSchema) {
    const std::string json = render("json");
    EXPECT_TRUE(json.starts_with("{\n"));
    EXPECT_TRUE(json.ends_with("}\n"));
    for (const char* key : {"\"tool\"", "\"run\"", "\"totals\"", "\"classes\"", "\"fingerprint\"", "\"query_time_us\"",
                            "\"p95\"", "\"latency_decades\"", "\"sample\"", "\"findings\"", "\"rule_id\"",
                            "\"suggestion\"", "\"class_count\"", "\"time_share\""}) {
        EXPECT_NE(json.find(key), std::string::npos) << key;
    }
    int depth = 0;
    bool in_string = false;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (in_string) {
            if (c == '\\') ++i;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
            ASSERT_GE(depth, 0);
        }
    }
    EXPECT_EQ(depth, 0);
}

TEST(MarkdownReporter, WritesTablesAndSqlBlocks) {
    const std::string md = render("markdown");
    EXPECT_TRUE(md.starts_with("# 🐌 SnailTrail report"));
    EXPECT_NE(md.find("## Top queries"), std::string::npos);
    EXPECT_NE(md.find("| # | Query |"), std::string::npos);
    EXPECT_NE(md.find("```sql"), std::string::npos);
    EXPECT_NE(md.find("<details><summary>Worst sample"), std::string::npos);
    EXPECT_EQ(render("md"), md);
}

TEST(Reporter, FactoryKnowsItsFormats) {
    EXPECT_EQ(report::reporter_formats().size(), 3U);
    EXPECT_EQ(report::make_reporter("JSON")->format_name(), "json");
    EXPECT_THROW(static_cast<void>(report::make_reporter("xml")), std::invalid_argument);
}

TEST(Reporter, RendersStandaloneFindings) {
    const std::vector<advisor::Finding> findings = {
        {"ST002", "unbounded-write", advisor::Severity::Critical, "UPDATE without WHERE", "Every row.",
         "Add a WHERE clause.\nOr batch it."}};
    std::ostringstream text;
    report::render_findings(findings, text, {}, 0);
    EXPECT_EQ(text.str(), "● ST002 unbounded-write · UPDATE without WHERE\n  Every row.\n"
                          "  → Add a WHERE clause.\n    Or batch it.\n");
    std::ostringstream json;
    report::write_findings_json(findings, json, 0);
    EXPECT_EQ(json.str(), R"([{"rule_id":"ST002","rule_name":"unbounded-write","severity":"critical",)"
                          R"("title":"UPDATE without WHERE","detail":"Every row.","suggestion":"Add a WHERE clause.\nOr batch it."}])");
}

TEST(TextReporter, ReportsAnEmptyLog) {
    std::ostringstream out;
    report::TextReporter().render(analysis::Analyzer().analyze_text(""), out);
    EXPECT_NE(out.str().find("No query events found."), std::string::npos);
}
