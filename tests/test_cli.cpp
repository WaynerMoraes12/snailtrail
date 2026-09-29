#include <gtest/gtest.h>

#include "support.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "arguments.hpp"
#include "command.hpp"

using namespace snailtrail::cli;

namespace {

struct CliResult {
    int code;
    std::string out;
    std::string err;
};

CliResult run(std::vector<std::string> args, const std::string& input = {}) {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in(input);
    Console console{out, err, in, false, 100};
    const int code = App().run(args, console);
    return {code, out.str(), err.str()};
}

std::string samples(const std::string& name) { return snailtrail::testing::sample_path(name); }

class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() / "snailtrail-cli-test") {
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    std::filesystem::path path_;
};

constexpr OptionSpec specs[] = {
    {"top", 'n', "N", ""},
    {"format", 'f', "FORMAT", ""},
    {"verbose", 'v', "", ""},
};

}

TEST(Arguments, ParsesLongShortInlineAndPositional) {
    const std::vector<std::string> args = {"slow.log", "-n", "5", "--format=json", "-v", "--", "--literal"};
    const Arguments a(specs, args);
    EXPECT_EQ(a.number<int>("top", 0), 5);
    EXPECT_EQ(a.value("format"), "json");
    EXPECT_TRUE(a.has("verbose"));
    EXPECT_FALSE(a.has("missing"));
    EXPECT_EQ(a.value_or("missing", "x"), "x");
    EXPECT_EQ(a.positionals(), (std::vector<std::string>{"slow.log", "--literal"}));
}

TEST(Arguments, RejectsMistakes) {
    auto parse = [](std::vector<std::string> args) { return Arguments(specs, args); };
    EXPECT_THROW(parse({"--nope"}), UsageError);
    EXPECT_THROW(parse({"-x"}), UsageError);
    EXPECT_THROW(parse({"--top"}), UsageError);
    EXPECT_THROW(parse({"--verbose=yes"}), UsageError);
    EXPECT_THROW(static_cast<void>(parse({"--top", "many"}).number<int>("top", 0)), UsageError);
    EXPECT_NO_THROW(parse({"-"}));
}

TEST(Cli, PrintsHelpAndVersion) {
    EXPECT_EQ(run({}).code, ExitCode::Usage);
    const CliResult help = run({"help"});
    EXPECT_EQ(help.code, ExitCode::Ok);
    EXPECT_NE(help.out.find("analyze"), std::string::npos);
    EXPECT_NE(run({"help", "analyze"}).out.find("--schema FILE"), std::string::npos);
    EXPECT_NE(run({"analyze", "--help"}).out.find("--fail-on LEVEL"), std::string::npos);
    EXPECT_EQ(run({"--version"}).out, "snailtrail " SNAILTRAIL_VERSION "\n");
    const CliResult bogus = run({"bogus"});
    EXPECT_EQ(bogus.code, ExitCode::Usage);
    EXPECT_NE(bogus.err.find("unknown command 'bogus'"), std::string::npos);
}

TEST(Cli, GeneratesAndAnalyzesALog) {
    TempDir dir;
    const std::string log = dir.file("slow.log");
    ASSERT_EQ(run({"generate", "--events", "3000", "--seed", "3", "--output", log}).code, ExitCode::Ok);

    const CliResult text = run({"analyze", log, "--schema", samples("shop_schema.sql"), "--top", "5", "--details", "1"});
    EXPECT_EQ(text.code, ExitCode::Ok) << text.err;
    EXPECT_NE(text.out.find("SnailTrail " SNAILTRAIL_VERSION), std::string::npos);
    EXPECT_NE(text.out.find("6 tables"), std::string::npos);
    EXPECT_NE(text.out.find("── #1 · "), std::string::npos);

    const CliResult json = run({"analyze", log, "--format", "json", "--top", "0"});
    EXPECT_EQ(json.code, ExitCode::Ok);
    EXPECT_TRUE(json.out.starts_with("{"));

    const std::string md = dir.file("report.md");
    const CliResult markdown = run({"analyze", log, "-f", "markdown", "-o", md});
    EXPECT_EQ(markdown.code, ExitCode::Ok);
    EXPECT_NE(markdown.err.find("wrote a markdown report"), std::string::npos);
    std::ifstream written(md);
    std::string first_line;
    std::getline(written, first_line);
    EXPECT_EQ(first_line, "# 🐌 SnailTrail report");

    EXPECT_EQ(run({"analyze", log, "--fail-on", "critical"}).code, ExitCode::FindingsAtThreshold);
    EXPECT_EQ(run({"analyze", log, "--fail-on", "critical", "--no-advice"}).code, ExitCode::Ok);
}

TEST(Cli, AnalyzesStandardInput) {
    const CliResult generated = run({"generate", "--events", "500"});
    const CliResult analyzed = run({"analyze", "-", "--no-color", "--top", "3"}, generated.out);
    EXPECT_EQ(analyzed.code, ExitCode::Ok);
    EXPECT_NE(analyzed.out.find("<stdin>"), std::string::npos);
}

TEST(Cli, ReportsAnalyzeErrors) {
    EXPECT_EQ(run({"analyze"}).code, ExitCode::Usage);
    EXPECT_EQ(run({"analyze", "a.log", "b.log"}).code, ExitCode::Usage);
    EXPECT_EQ(run({"analyze", "-", "--sort", "speed"}).code, ExitCode::Usage);
    EXPECT_EQ(run({"analyze", "-", "--disable", "no-such-rule"}).code, ExitCode::Usage);
    EXPECT_EQ(run({"analyze", "-", "--min-severity", "loud"}).code, ExitCode::Usage);
    EXPECT_EQ(run({"analyze", "-", "--format", "xml"}).code, ExitCode::Failure);
    const CliResult missing = run({"analyze", "/no/such/slow.log"});
    EXPECT_EQ(missing.code, ExitCode::Failure);
    EXPECT_NE(missing.err.find("cannot open"), std::string::npos);
}

TEST(Cli, AdvisesOnAStatement) {
    const CliResult r = run({"advise", "UPDATE customers SET tier = 1"});
    EXPECT_EQ(r.code, ExitCode::Ok);
    EXPECT_NE(r.out.find("● ST002 unbounded-write"), std::string::npos);
    EXPECT_EQ(run({"advise", "UPDATE customers SET tier = 1", "--fail-on", "critical"}).code,
              ExitCode::FindingsAtThreshold);

    const CliResult schema = run({"advise", "SELECT id FROM customers WHERE phone = 551199", "--schema",
                            samples("shop_schema.sql"), "--format", "json"});
    EXPECT_NE(schema.out.find("\"rule_id\": \"ST005\""), std::string::npos);

    const CliResult clean = run({"advise", "SELECT id FROM customers WHERE email = 'a@b.c'", "-s", samples("shop_schema.sql")});
    EXPECT_NE(clean.out.find("No findings"), std::string::npos);

    const CliResult piped = run({"advise", "-", "--disable", "ST013"}, "SELECT * FROM t WHERE id NOT IN (SELECT t_id FROM u)");
    EXPECT_NE(piped.out.find("ST007"), std::string::npos);
    EXPECT_EQ(piped.out.find("ST013"), std::string::npos);
}

TEST(Cli, FingerprintsParsesAndListsRules) {
    const CliResult fp = run({"fingerprint", "SELECT * FROM t WHERE id = 42"});
    EXPECT_NE(fp.out.find("SELECT  select * from t where id = ?"), std::string::npos);
    const CliResult fp_json = run({"fingerprint", "--json", "SELECT 1"});
    EXPECT_NE(fp_json.out.find("\"fingerprint\": \"select ?\""), std::string::npos);

    const CliResult tree = run({"parse", "SELECT a FROM t", "--sql"});
    EXPECT_NE(tree.out.find("└── FROM"), std::string::npos);
    EXPECT_NE(tree.out.find("SELECT a FROM t\n"), std::string::npos);
    const CliResult bad = run({"parse", "SELECT a FROM t WHERE"});
    EXPECT_EQ(bad.code, ExitCode::Failure);
    EXPECT_NE(bad.err.find("^"), std::string::npos);

    const CliResult rules = run({"rules"});
    EXPECT_NE(rules.out.find("ST015"), std::string::npos);
    EXPECT_NE(run({"rules", "--json"}).out.find("\"needs_schema\": true"), std::string::npos);
}
