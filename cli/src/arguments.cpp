#include "arguments.hpp"

namespace snailtrail::cli {

Arguments::Arguments(std::span<const OptionSpec> specs, std::span<const std::string> args) : specs_(specs) {
    bool options_done = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (options_done || arg == "-" || !arg.starts_with('-')) {
            positionals_.push_back(arg);
            continue;
        }
        if (arg == "--") {
            options_done = true;
            continue;
        }

        const OptionSpec* spec = nullptr;
        std::optional<std::string> inline_value;
        if (arg.starts_with("--")) {
            std::string_view body = std::string_view(arg).substr(2);
            const std::size_t eq = body.find('=');
            if (eq != std::string_view::npos) {
                inline_value = std::string(body.substr(eq + 1));
                body = body.substr(0, eq);
            }
            spec = find(body);
            if (spec == nullptr) throw UsageError("unknown option --" + std::string(body));
        } else {
            if (arg.size() != 2) throw UsageError("unknown option " + arg);
            spec = find(arg[1]);
            if (spec == nullptr) throw UsageError("unknown option " + arg);
        }

        Given given{std::string(spec->name), std::nullopt};
        if (!spec->value_name.empty()) {
            if (inline_value) {
                given.value = std::move(inline_value);
            } else if (i + 1 < args.size()) {
                given.value = args[++i];
            } else {
                throw UsageError("--" + std::string(spec->name) + " needs a value (" +
                                 std::string(spec->value_name) + ")");
            }
        } else if (inline_value) {
            throw UsageError("--" + std::string(spec->name) + " takes no value");
        }
        given_.push_back(std::move(given));
    }
}

const OptionSpec* Arguments::find(std::string_view name) const noexcept {
    for (const auto& s : specs_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const OptionSpec* Arguments::find(char short_name) const noexcept {
    for (const auto& s : specs_) {
        if (s.short_name != '\0' && s.short_name == short_name) return &s;
    }
    return nullptr;
}

bool Arguments::has(std::string_view name) const noexcept {
    for (const auto& g : given_) {
        if (g.name == name) return true;
    }
    return false;
}

std::optional<std::string> Arguments::value(std::string_view name) const {
    std::optional<std::string> last;
    for (const auto& g : given_) {
        if (g.name == name) last = g.value;
    }
    return last;
}

std::string Arguments::value_or(std::string_view name, std::string_view fallback) const {
    return value(name).value_or(std::string(fallback));
}

}
