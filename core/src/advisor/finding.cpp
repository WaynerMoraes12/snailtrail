#include "snailtrail/advisor/finding.hpp"

#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

std::string_view severity_name(Severity severity) noexcept {
    switch (severity) {
    case Severity::Info: return "info";
    case Severity::Warning: return "warning";
    case Severity::Critical: return "critical";
    }
    return "info";
}

std::optional<Severity> parse_severity(std::string_view name) noexcept {
    if (util::iequals(name, "info")) return Severity::Info;
    if (util::iequals(name, "warning")) return Severity::Warning;
    if (util::iequals(name, "critical")) return Severity::Critical;
    return std::nullopt;
}

}
