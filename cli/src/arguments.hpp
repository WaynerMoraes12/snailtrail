#pragma once

#include <charconv>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace snailtrail::cli {

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct OptionSpec {
    std::string_view name;
    char short_name = '\0';
    std::string_view value_name;
    std::string_view help;
};

class Arguments {
public:
    Arguments(std::span<const OptionSpec> specs, std::span<const std::string> args);

    [[nodiscard]] bool has(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::string> value(std::string_view name) const;
    [[nodiscard]] std::string value_or(std::string_view name, std::string_view fallback) const;
    [[nodiscard]] const std::vector<std::string>& positionals() const noexcept { return positionals_; }

    template <typename T>
    [[nodiscard]] T number(std::string_view name, T fallback) const {
        const auto text = value(name);
        if (!text) return fallback;
        T result{};
        const auto [ptr, ec] = std::from_chars(text->data(), text->data() + text->size(), result);
        if (ec != std::errc{} || ptr != text->data() + text->size()) {
            throw UsageError("--" + std::string(name) + " expects a number, got '" + *text + "'");
        }
        return result;
    }

private:
    struct Given {
        std::string name;
        std::optional<std::string> value;
    };

    const OptionSpec* find(std::string_view name) const noexcept;
    const OptionSpec* find(char short_name) const noexcept;

    std::span<const OptionSpec> specs_;
    std::vector<Given> given_;
    std::vector<std::string> positionals_;
};

}
