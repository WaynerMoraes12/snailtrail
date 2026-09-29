#include <array>
#include <format>

#include "command.hpp"
#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/util/json_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

namespace {

constexpr std::array<OptionSpec, 1> rules_options = {{
    {"json", '\0', "", "print a JSON array"},
}};

std::string needs(const advisor::RuleInfo& info) {
    std::vector<std::string> parts;
    if (info.needs_schema) parts.emplace_back("schema");
    if (info.needs_stats) parts.emplace_back("slow log");
    return parts.empty() ? "-" : util::join(parts, ", ");
}

class RulesCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "rules"; }
    [[nodiscard]] std::string_view summary() const noexcept override { return "List the advisor rules"; }
    [[nodiscard]] std::string_view usage() const noexcept override { return "rules [--json]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return rules_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        const auto engine = advisor::RuleEngine::with_default_rules();
        if (args.has("json")) {
            util::JsonWriter w(console.out);
            w.begin_array();
            for (const advisor::Rule* r : engine.rules()) {
                const auto& i = r->info();
                w.begin_object()
                    .field("id", i.id)
                    .field("name", i.name)
                    .field("severity", advisor::severity_name(i.severity))
                    .field("needs_schema", i.needs_schema)
                    .field("needs_stats", i.needs_stats)
                    .field("summary", i.summary)
                    .end_object();
            }
            w.end_array();
            return ExitCode::Ok;
        }
        console.out << std::format("{:<6} {:<26} {:<9} {:<10} {}\n", "ID", "NAME", "SEVERITY", "NEEDS", "CHECKS");
        for (const advisor::Rule* r : engine.rules()) {
            const auto& i = r->info();
            console.out << std::format("{:<6} {:<26} {:<9} {:<10} {}\n", i.id, i.name,
                                       advisor::severity_name(i.severity), needs(i), i.summary);
        }
        return ExitCode::Ok;
    }
};

}

std::unique_ptr<Command> make_rules_command() { return std::make_unique<RulesCommand>(); }

}
