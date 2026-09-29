#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace snailtrail::advisor {

enum class Severity : std::uint8_t { Info, Warning, Critical };

std::string_view severity_name(Severity severity) noexcept;
std::optional<Severity> parse_severity(std::string_view name) noexcept;

struct Finding {
    std::string rule_id;
    std::string rule_name;
    Severity severity = Severity::Info;
    std::string title;
    std::string detail;
    std::string suggestion;
};

}
