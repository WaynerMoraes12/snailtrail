#include <array>
#include <format>

#include "command.hpp"
#include "snailtrail/sql/ast_printer.hpp"
#include "snailtrail/sql/fingerprint.hpp"
#include "snailtrail/sql/parser.hpp"
#include "snailtrail/sql/sql_writer.hpp"
#include "snailtrail/util/json_writer.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

namespace {

std::string statement_from(const Arguments& args, Console& console) {
    if (args.positionals().empty()) throw UsageError("expected a SQL statement (or - for standard input)");
    if (args.positionals().front() == "-") return read_all(console.in);
    return util::join(args.positionals(), " ");
}

constexpr std::array<OptionSpec, 1> fingerprint_options = {{
    {"json", '\0', "", "print a JSON object"},
}};

class FingerprintCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "fingerprint"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Print the normalized fingerprint and class id of a statement";
    }
    [[nodiscard]] std::string_view usage() const noexcept override { return "fingerprint <\"SQL\" | -> [--json]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return fingerprint_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        const auto fp = sql::fingerprint(statement_from(args, console));
        if (args.has("json")) {
            util::JsonWriter w(console.out);
            w.begin_object()
                .field("id", fp.id_hex())
                .field("kind", sql::statement_kind_name(fp.kind))
                .field("fingerprint", fp.text)
                .end_object();
        } else {
            console.out << fp.id_hex() << "  " << sql::statement_kind_name(fp.kind) << "  " << fp.text << '\n';
        }
        return ExitCode::Ok;
    }
};

constexpr std::array<OptionSpec, 1> parse_options = {{
    {"sql", '\0', "", "also print the statement back as canonical SQL (SELECT only)"},
}};

class ParseCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "parse"; }
    [[nodiscard]] std::string_view summary() const noexcept override {
        return "Print the syntax tree SnailTrail builds for a statement";
    }
    [[nodiscard]] std::string_view usage() const noexcept override { return "parse <\"SQL\" | -> [--sql]"; }
    [[nodiscard]] std::span<const OptionSpec> options() const noexcept override { return parse_options; }

    [[nodiscard]] int run(const Arguments& args, Console& console) const override {
        const std::string text = statement_from(args, console);
        try {
            const auto statement = sql::parse(text);
            console.out << sql::dump_ast(*statement);
            if (args.has("sql")) {
                if (const auto* select = dynamic_cast<const sql::SelectStatement*>(statement.get())) {
                    console.out << '\n' << sql::to_sql(*select) << '\n';
                }
            }
            return ExitCode::Ok;
        } catch (const sql::ParseError& e) {
            const std::size_t line_start = text.rfind('\n', e.offset() == 0 ? 0 : e.offset() - 1);
            const std::size_t begin = line_start == std::string::npos ? 0 : line_start + 1;
            const std::size_t end = text.find('\n', e.offset());
            const std::string_view line = std::string_view(text).substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            console.err << "snailtrail parse: " << e.what() << "\n  " << line << "\n  "
                        << std::string(util::display_width(std::string_view(text).substr(begin, e.offset() - begin)), ' ')
                        << "^\n";
            return ExitCode::Failure;
        }
    }
};

}

std::unique_ptr<Command> make_fingerprint_command() { return std::make_unique<FingerprintCommand>(); }
std::unique_ptr<Command> make_parse_command() { return std::make_unique<ParseCommand>(); }

}
