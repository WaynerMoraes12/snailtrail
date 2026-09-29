#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/advisor/finding.hpp"
#include "snailtrail/stats/aggregator.hpp"
#include "snailtrail/stats/query_class.hpp"

namespace snailtrail::analysis {

enum class SortKey : std::uint8_t { TotalTime, Calls, AverageTime, P95, MaxTime, RowsExamined };

std::string_view sort_key_name(SortKey key) noexcept;
std::optional<SortKey> parse_sort_key(std::string_view name) noexcept;

struct ClassReport {
    std::size_t rank = 0;
    stats::QueryClass stats;
    double time_share = 0.0;
    std::vector<std::string> tables;
    std::vector<advisor::Finding> findings;

    [[nodiscard]] std::optional<advisor::Severity> worst_severity() const noexcept;
    [[nodiscard]] std::string label() const;
};

struct RunInfo {
    std::string source;
    std::string version = SNAILTRAIL_VERSION;
    std::uint64_t bytes = 0;
    std::uint64_t lines = 0;
    std::uint64_t skipped = 0;
    std::uint64_t filtered = 0;
    unsigned threads = 1;
    std::size_t chunks = 1;
    double parse_seconds = 0.0;
    double elapsed_seconds = 0.0;
    std::size_t schema_tables = 0;
    std::string database_filter;
};

struct Report {
    RunInfo run;
    stats::Totals totals;
    SortKey sort = SortKey::TotalTime;
    std::size_t class_count = 0;
    std::vector<ClassReport> classes;

    [[nodiscard]] std::array<std::size_t, 3> findings_by_severity() const noexcept;
};

}
