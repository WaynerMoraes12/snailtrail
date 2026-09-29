#include <algorithm>
#include <array>
#include <fstream>
#include <optional>

#include "command.hpp"
#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/analysis/analyzer.hpp"
#include "snailtrail/report/reporter.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

namespace {

constexpr std::array<OptionSpec, 15> analyze_options = {{
    {"schema", 's', "FILE", "schema DDL (mysqldump --no-data) to check indexes and column types"},
    {"format", 'f', "FORMAT", "text, json or markdown (default: text)"},
    {"output", 'o', "FILE", "write the report to FILE instead of standard output"},
    {"threads", 't', "N", "worker threads (default: every core)"},
    {"top", 'n', "N", "keep the N most expensive query classes (default: 20, 0 = all)"},
    {"details", '\0', "N", "detail the first N classes in text and markdown (default: 10)"},
    {"sort", '\0', "KEY", "rank by time, calls, avg, p95, max or rows (default: time)"},
    {"database", 'd', "NAME", "only analyse statements run against this database"},
    {"min-severity", '\0', "LEVEL", "hide findings below info, warning or critical"},
    {"disable", '\0', "RULES", "comma-separated rule ids or names to switch off"},
    {"fail-on", '\0', "LEVEL", "exit with status 2 if a finding reaches this severity"},
    {"no-advice", '\0', "", "profile only, skip the advisor"},
    {"width", 'w', "N", "text width (default: terminal width or 100)"},
    {"color", '\0', "", "force colors"},
    {"no-color", '\0', "", "disable colors"},
}};

advisor::Severity severity_option(const Arguments& args, std::string_view name) {
    const std::string text = args.value_or(name, "info");
    const auto s = advisor::parse_severity(text);
    if (!s) throw UsageError("--" + std::string(name) + " expects info, warning or critical, got '" + text + "'");
    return *s;
}

class AnalyzeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "analyze"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Profile a MySQL slow query log and advise on its most expensive queries";
    }
    [[nodiscard]] std::string_view usage() const noexcept override { return "analyze <slow.log | -> [options]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return analyze_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        if (args.positionals().size() != 1) {
            throw UsageError("expected one slow log path (or - for standard input)");
        }
        const std::string& path = args.positionals().front();

        std::optional<schema::SchemaCatalog> catalog;
        if (const auto schema_path = args.value("schema")) {
            std::vector<std::string> warnings;
            catalog = schema::SchemaCatalog::from_ddl(read_file(*schema_path), &warnings);
            for (const auto& w : warnings) console.err << "snailtrail: " << *schema_path << ": " << w << '\n';
        }

        advisor::RuleEngine engine = advisor::RuleEngine::with_default_rules();
        if (const auto disabled = args.value("disable")) {
            for (std::string_view id : util::split(*disabled, ',')) {
                id = util::trim(id);
                if (!id.empty() && !engine.disable(id)) throw UsageError("unknown rule '" + std::string(id) + "'");
            }
        }

        analysis::AnalyzeOptions options;
        options.threads = args.number<unsigned>("threads", 0);
        options.top = args.number<std::size_t>("top", 20);
        options.database = args.value_or("database", "");
        options.min_severity = severity_option(args, "min-severity");
        options.advise = !args.has("no-advice");
        if (const auto sort = args.value("sort")) {
            const auto key = analysis::parse_sort_key(*sort);
            if (!key) throw UsageError("--sort expects time, calls, avg, p95, max or rows");
            options.sort = *key;
        }

        const analysis::Analyzer analyzer(options, catalog ? &*catalog : nullptr, &engine);
        const analysis::Report report = path == "-" ? analyzer.analyze_stream(console.in)
                                                    : analyzer.analyze_file(path);

        const auto output = args.value("output");
        report::RenderOptions render;
        render.color = !output && color_choice(args, console.color);
        render.width = args.number<std::size_t>("width", console.width);
        render.detail_limit = args.number<std::size_t>("details", 10);
        const auto reporter = report::make_reporter(args.value_or("format", "text"), render);
        if (output) {
            std::ofstream file(*output, std::ios::binary);
            if (!file) throw std::runtime_error("cannot write " + *output);
            reporter->render(report, file);
            console.err << "snailtrail: wrote a " << reporter->format_name() << " report to " << *output << '\n';
        } else {
            reporter->render(report, console.out);
        }

        if (args.has("fail-on")) {
            const advisor::Severity threshold = severity_option(args, "fail-on");
            for (const auto& c : report.classes) {
                for (const auto& f : c.findings) {
                    if (f.severity >= threshold) return ExitCode::FindingsAtThreshold;
                }
            }
        }
        return ExitCode::Ok;
    }
};

}

std::unique_ptr<Command> make_analyze_command() { return std::make_unique<AnalyzeCommand>(); }

}
