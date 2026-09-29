#include <array>
#include <optional>

#include "command.hpp"
#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/report/reporter.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

namespace {

constexpr std::array<OptionSpec, 7> advise_options = {{
    {"schema", 's', "FILE", "schema DDL (mysqldump --no-data) to check indexes and column types"},
    {"format", 'f', "FORMAT", "text or json (default: text)"},
    {"disable", '\0', "RULES", "comma-separated rule ids or names to switch off"},
    {"fail-on", '\0', "LEVEL", "exit with status 2 if a finding reaches this severity"},
    {"width", 'w', "N", "text width (default: terminal width or 100)"},
    {"color", '\0', "", "force colors"},
    {"no-color", '\0', "", "disable colors"},
}};

class AdviseCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "advise"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Check one SQL statement against the advisor rules";
    }
    [[nodiscard]] std::string_view usage() const noexcept override { return "advise <\"SQL\" | -> [options]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return advise_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        if (args.positionals().empty()) throw UsageError("expected a SQL statement (or - for standard input)");
        const std::string sql = args.positionals().front() == "-" ? read_all(console.in)
                                                                   : util::join(args.positionals(), " ");

        std::optional<schema::SchemaCatalog> catalog;
        if (const auto schema_path = args.value("schema")) {
            catalog = schema::SchemaCatalog::from_ddl(read_file(*schema_path));
        }
        advisor::RuleEngine engine = advisor::RuleEngine::with_default_rules();
        if (const auto disabled = args.value("disable")) {
            for (std::string_view id : util::split(*disabled, ',')) {
                id = util::trim(id);
                if (!id.empty() && !engine.disable(id)) throw UsageError("unknown rule '" + std::string(id) + "'");
            }
        }

        const auto findings = engine.advise(sql, catalog ? &*catalog : nullptr);
        const std::string format = args.value_or("format", "text");
        if (util::iequals(format, "json")) {
            report::write_findings_json(findings, console.out);
        } else if (util::iequals(format, "text")) {
            report::RenderOptions render;
            render.color = color_choice(args, console.color);
            render.width = args.number<std::size_t>("width", console.width);
            if (findings.empty()) {
                console.out << "No findings: nothing in this statement matches a rule.\n";
            } else {
                report::render_findings(findings, console.out, render, 0);
            }
        } else {
            throw UsageError("--format expects text or json");
        }

        if (const auto level = args.value("fail-on")) {
            const auto threshold = advisor::parse_severity(*level);
            if (!threshold) throw UsageError("--fail-on expects info, warning or critical");
            for (const auto& f : findings) {
                if (f.severity >= *threshold) return ExitCode::FindingsAtThreshold;
            }
        }
        return ExitCode::Ok;
    }
};

}

std::unique_ptr<Command> make_advise_command() { return std::make_unique<AdviseCommand>(); }

}
