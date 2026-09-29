#pragma once

#include <cstddef>
#include <istream>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arguments.hpp"

namespace snailtrail::cli {

enum ExitCode : int {
    Ok = 0,
    Failure = 1,
    FindingsAtThreshold = 2,
    Usage = 64,
};

struct Console {
    std::ostream& out;
    std::ostream& err;
    std::istream& in;
    bool color = false;
    std::size_t width = 100;
};

class Command {
public:
    Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;
    virtual ~Command() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view summary() const noexcept = 0;
    [[nodiscard]] virtual std::string_view usage() const noexcept = 0;
    [[nodiscard]] virtual std::span<const OptionSpec> options() const noexcept = 0;
    [[nodiscard]] virtual int run(const Arguments& args, Console& console) const = 0;
};

std::unique_ptr<Command> make_analyze_command();
std::unique_ptr<Command> make_advise_command();
std::unique_ptr<Command> make_fingerprint_command();
std::unique_ptr<Command> make_parse_command();
std::unique_ptr<Command> make_rules_command();
std::unique_ptr<Command> make_generate_command();

class App {
public:
    App();

    int run(std::span<const std::string> args, Console& console) const;

    void print_help(std::ostream& out) const;
    static void print_command_help(const Command& command, std::ostream& out);
    [[nodiscard]] const Command* find(std::string_view name) const noexcept;

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

std::string read_all(std::istream& in);
std::string read_file(const std::string& path);
bool color_choice(const Arguments& args, bool terminal);

}
