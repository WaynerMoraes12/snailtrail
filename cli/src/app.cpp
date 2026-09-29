#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

#include "command.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::cli {

App::App() {
    commands_.push_back(make_analyze_command());
    commands_.push_back(make_advise_command());
    commands_.push_back(make_fingerprint_command());
    commands_.push_back(make_parse_command());
    commands_.push_back(make_rules_command());
    commands_.push_back(make_generate_command());
}

const Command* App::find(std::string_view name) const noexcept {
    for (const auto& c : commands_) {
        if (c->name() == name) return c.get();
    }
    return nullptr;
}

void App::print_help(std::ostream& out) const {
    out << "SnailTrail " SNAILTRAIL_VERSION " - every slow query leaves a trail.\n\n"
        << "Usage: snailtrail <command> [options]\n\nCommands:\n";
    for (const auto& c : commands_) {
        std::string name(c->name());
        name.resize(13, ' ');
        out << "  " << name << c->summary() << '\n';
    }
    out << "\nRun 'snailtrail help <command>' for its options.\n";
}

void App::print_command_help(const Command& command, std::ostream& out) {
    out << "Usage: snailtrail " << command.usage() << "\n\n" << command.summary() << "\n";
    if (command.options().empty()) return;
    out << "\nOptions:\n";
    for (const OptionSpec& o : command.options()) {
        std::string flag = o.short_name != '\0' ? std::string("-") + o.short_name + ", " : "    ";
        flag += "--" + std::string(o.name);
        if (!o.value_name.empty()) flag += " " + std::string(o.value_name);
        if (flag.size() < 26) flag.resize(26, ' ');
        out << "  " << flag << ' ' << o.help << '\n';
    }
}

int App::run(std::span<const std::string> args, Console& console) const {
    if (args.empty()) {
        print_help(console.err);
        return ExitCode::Usage;
    }
    const std::string& first = args.front();
    if (first == "help" || first == "--help" || first == "-h") {
        if (args.size() > 1) {
            if (const Command* c = find(args[1])) {
                print_command_help(*c, console.out);
                return ExitCode::Ok;
            }
            console.err << "snailtrail: unknown command '" << args[1] << "'\n";
            return ExitCode::Usage;
        }
        print_help(console.out);
        return ExitCode::Ok;
    }
    if (first == "version" || first == "--version" || first == "-V") {
        console.out << "snailtrail " SNAILTRAIL_VERSION "\n";
        return ExitCode::Ok;
    }

    const Command* command = find(first);
    if (command == nullptr) {
        console.err << "snailtrail: unknown command '" << first << "'\n\n";
        print_help(console.err);
        return ExitCode::Usage;
    }
    const auto rest = args.subspan(1);
    if (std::any_of(rest.begin(), rest.end(), [](const std::string& a) { return a == "--help" || a == "-h"; })) {
        print_command_help(*command, console.out);
        return ExitCode::Ok;
    }
    try {
        const Arguments parsed(command->options(), rest);
        return command->run(parsed, console);
    } catch (const UsageError& e) {
        console.err << "snailtrail " << command->name() << ": " << e.what() << "\nRun 'snailtrail help "
                    << command->name() << "' for usage.\n";
        return ExitCode::Usage;
    } catch (const std::exception& e) {
        console.err << "snailtrail " << command->name() << ": " << e.what() << '\n';
        return ExitCode::Failure;
    }
}

std::string read_all(std::istream& in) {
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return std::move(buffer).str();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    return read_all(in);
}

bool color_choice(const Arguments& args, bool terminal) {
    if (args.has("no-color")) return false;
    if (args.has("color")) return true;
    return terminal;
}

}
