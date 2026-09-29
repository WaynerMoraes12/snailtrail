#include <array>
#include <fstream>

#include "command.hpp"
#include "snailtrail/log/generator.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

namespace {

constexpr std::array<OptionSpec, 4> generate_options = {{
    {"events", 'e', "N", "number of events (default: 10000)"},
    {"seed", '\0', "N", "random seed; the same seed writes the same log (default: 42)"},
    {"output", 'o', "FILE", "write to FILE instead of standard output"},
    {"no-extra", '\0', "", "omit the MySQL 8 log_slow_extra counters"},
}};

class GenerateCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "generate"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Write a synthetic MySQL slow query log (demos and benchmarks)";
    }
    [[nodiscard]] std::string_view usage() const noexcept override { return "generate [options]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return generate_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        if (!args.positionals().empty()) throw UsageError("generate takes no positional arguments");
        log::GeneratorOptions options;
        options.events = args.number<std::uint64_t>("events", 10'000);
        options.seed = args.number<std::uint64_t>("seed", 42);
        options.extra_fields = !args.has("no-extra");
        log::SlowLogGenerator generator(options);

        if (const auto path = args.value("output")) {
            std::ofstream file(*path, std::ios::binary);
            if (!file) throw std::runtime_error("cannot write " + *path);
            generator.write(file);
            file.flush();
            console.err << "snailtrail: wrote " << util::format_count(options.events) << " events ("
                        << util::format_bytes(static_cast<std::uint64_t>(file.tellp())) << ") to " << *path << '\n';
        } else {
            generator.write(console.out);
        }
        return ExitCode::Ok;
    }
};

}

std::unique_ptr<Command> make_generate_command() { return std::make_unique<GenerateCommand>(); }

}
