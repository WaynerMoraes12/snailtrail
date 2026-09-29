#include "snailtrail/analysis/report.hpp"

#include "snailtrail/util/strings.hpp"

namespace snailtrail::analysis {

std::string_view sort_key_name(SortKey key) noexcept {
    switch (key) {
    case SortKey::TotalTime: return "time";
    case SortKey::Calls: return "calls";
    case SortKey::AverageTime: return "avg";
    case SortKey::P95: return "p95";
    case SortKey::MaxTime: return "max";
    case SortKey::RowsExamined: return "rows";
    }
    return "time";
}

std::optional<SortKey> parse_sort_key(std::string_view name) noexcept {
    for (SortKey k : {SortKey::TotalTime, SortKey::Calls, SortKey::AverageTime, SortKey::P95,
                      SortKey::MaxTime, SortKey::RowsExamined}) {
        if (util::iequals(name, sort_key_name(k))) return k;
    }
    return std::nullopt;
}

std::optional<advisor::Severity> ClassReport::worst_severity() const noexcept {
    std::optional<advisor::Severity> worst;
    for (const auto& f : findings) {
        if (!worst || f.severity > *worst) worst = f.severity;
    }
    return worst;
}

std::string ClassReport::label() const {
    std::string out(sql::statement_kind_name(stats.kind()));
    if (!tables.empty()) out += " " + util::join(tables, ", ");
    return out;
}

std::array<std::size_t, 3> Report::findings_by_severity() const noexcept {
    std::array<std::size_t, 3> counts{};
    for (const auto& c : classes) {
        for (const auto& f : c.findings) ++counts[static_cast<std::size_t>(f.severity)];
    }
    return counts;
}

}
