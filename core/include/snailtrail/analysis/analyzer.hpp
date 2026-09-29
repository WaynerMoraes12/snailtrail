#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

#include "snailtrail/advisor/finding.hpp"
#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/analysis/report.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/stats/aggregator.hpp"

namespace snailtrail::analysis {

struct AnalyzeOptions {
    unsigned threads = 0;
    std::size_t top = 0;
    SortKey sort = SortKey::TotalTime;
    std::size_t min_chunk_bytes = std::size_t{1} << 20;
    std::string database;
    advisor::Severity min_severity = advisor::Severity::Info;
    bool advise = true;
};

class Analyzer {
public:
    explicit Analyzer(AnalyzeOptions options = {}, const schema::SchemaCatalog* catalog = nullptr,
                      const advisor::RuleEngine* rules = nullptr);
    Analyzer(const Analyzer&) = delete;
    Analyzer& operator=(const Analyzer&) = delete;
    Analyzer(Analyzer&&) = delete;
    Analyzer& operator=(Analyzer&&) = delete;
    ~Analyzer() = default;

    [[nodiscard]] Report analyze_file(const std::filesystem::path& path) const;
    [[nodiscard]] Report analyze_text(std::string_view text, std::string source = "<memory>") const;
    [[nodiscard]] Report analyze_stream(std::istream& in, std::string source = "<stdin>") const;

    [[nodiscard]] const AnalyzeOptions& options() const noexcept { return options_; }
    [[nodiscard]] unsigned effective_threads() const noexcept;

private:
    [[nodiscard]] stats::Aggregator aggregate(std::string_view text, RunInfo& run) const;
    [[nodiscard]] Report build_report(stats::Aggregator aggregator, RunInfo run) const;
    [[nodiscard]] bool accepts(std::string_view database) const noexcept;

    AnalyzeOptions options_;
    const schema::SchemaCatalog* catalog_;
    advisor::RuleEngine default_rules_;
    const advisor::RuleEngine* rules_;
};

}
