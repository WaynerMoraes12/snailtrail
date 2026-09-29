#include "snailtrail/analysis/analyzer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/log/chunker.hpp"
#include "snailtrail/log/mapped_file.hpp"
#include "snailtrail/log/slow_log_parser.hpp"
#include "snailtrail/sql/parser.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::analysis {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double sort_value(const stats::QueryClass& c, SortKey key) {
    switch (key) {
    case SortKey::TotalTime: return static_cast<double>(c.query_time().sum());
    case SortKey::Calls: return static_cast<double>(c.calls());
    case SortKey::AverageTime: return c.query_time().mean();
    case SortKey::P95: return static_cast<double>(c.percentile_us(0.95));
    case SortKey::MaxTime: return static_cast<double>(c.query_time().max());
    case SortKey::RowsExamined: return static_cast<double>(c.rows_examined().sum());
    }
    return 0.0;
}

}

Analyzer::Analyzer(AnalyzeOptions options, const schema::SchemaCatalog* catalog,
                   const advisor::RuleEngine* rules)
    : options_(std::move(options)), catalog_(catalog),
      default_rules_(rules == nullptr ? advisor::RuleEngine::with_default_rules() : advisor::RuleEngine{}),
      rules_(rules == nullptr ? &default_rules_ : rules) {}

unsigned Analyzer::effective_threads() const noexcept {
    if (options_.threads > 0) return options_.threads;
    return std::max(1U, std::thread::hardware_concurrency());
}

bool Analyzer::accepts(std::string_view database) const noexcept {
    return options_.database.empty() || util::iequals(database, options_.database);
}

stats::Aggregator Analyzer::aggregate(std::string_view text, RunInfo& run) const {
    const auto chunks = log::split_log(text, effective_threads(), options_.min_chunk_bytes);
    std::vector<stats::Aggregator> partials(chunks.size());
    std::vector<log::SlowLogParser::Counters> counters(chunks.size());
    std::vector<std::uint64_t> filtered(chunks.size(), 0);
    std::vector<std::exception_ptr> errors(chunks.size());

    auto work = [&](std::size_t i) {
        try {
            counters[i] = log::SlowLogParser::parse_text(
                chunks[i].text,
                [&, i](const log::QueryEvent& e) {
                    if (accepts(e.database)) {
                        partials[i].add(e);
                    } else {
                        ++filtered[i];
                    }
                },
                chunks[i].initial_database);
        } catch (...) {
            errors[i] = std::current_exception();
        }
    };

    if (chunks.size() == 1) {
        work(0);
    } else {
        std::vector<std::jthread> workers;
        workers.reserve(chunks.size());
        for (std::size_t i = 0; i < chunks.size(); ++i) workers.emplace_back(work, i);
    }
    for (const auto& e : errors) {
        if (e) std::rethrow_exception(e);
    }

    stats::Aggregator total = std::move(partials.front());
    for (std::size_t i = 1; i < partials.size(); ++i) total.merge(std::move(partials[i]));
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        run.lines += counters[i].lines;
        run.skipped += counters[i].skipped;
        run.filtered += filtered[i];
    }
    run.chunks = chunks.size();
    run.threads = static_cast<unsigned>(chunks.size());
    return total;
}

Report Analyzer::build_report(stats::Aggregator aggregator, RunInfo run) const {
    Report report;
    report.sort = options_.sort;
    report.totals = aggregator.totals();
    run.schema_tables = catalog_ == nullptr ? 0 : catalog_->size();
    run.database_filter = options_.database;

    std::vector<stats::QueryClass> classes = aggregator.take_classes();
    report.class_count = classes.size();
    std::stable_sort(classes.begin(), classes.end(), [&](const stats::QueryClass& a, const stats::QueryClass& b) {
        const double va = sort_value(a, options_.sort);
        const double vb = sort_value(b, options_.sort);
        return va != vb ? va > vb : a.id() < b.id();
    });
    if (options_.top > 0 && classes.size() > options_.top) {
        classes.erase(classes.begin() + static_cast<std::ptrdiff_t>(options_.top), classes.end());
    }

    const double total_time = static_cast<double>(report.totals.query_time_us);
    report.classes.reserve(classes.size());
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const double share =
            total_time > 0 ? static_cast<double>(classes[i].query_time().sum()) / total_time : 0.0;
        report.classes.push_back(ClassReport{i + 1, std::move(classes[i]), share, {}, {}});
    }

    for (ClassReport& c : report.classes) {
        const std::string& sample = c.stats.worst().sql;
        std::string error;
        const sql::StatementPtr statement = sql::try_parse(sample, &error);
        advisor::QueryFacts facts;
        if (statement) {
            facts = advisor::collect_facts(*statement, catalog_);
            for (const auto& t : facts.tables) {
                if (!t.derived && std::find(c.tables.begin(), c.tables.end(), t.name) == c.tables.end()) {
                    c.tables.push_back(t.name);
                }
            }
        }
        if (!options_.advise) continue;
        auto findings = rules_->advise(sample, statement.get(), statement ? &facts : nullptr, error, catalog_,
                                       &c.stats);
        std::erase_if(findings, [&](const advisor::Finding& f) { return f.severity < options_.min_severity; });
        c.findings = std::move(findings);
    }

    report.run = std::move(run);
    return report;
}

Report Analyzer::analyze_text(std::string_view text, std::string source) const {
    const auto start = Clock::now();
    RunInfo run;
    run.source = std::move(source);
    run.bytes = text.size();
    stats::Aggregator aggregator = aggregate(text, run);
    run.parse_seconds = seconds_since(start);
    Report report = build_report(std::move(aggregator), std::move(run));
    report.run.elapsed_seconds = seconds_since(start);
    return report;
}

Report Analyzer::analyze_file(const std::filesystem::path& path) const {
    const auto start = Clock::now();
    const log::MappedFile file(path);
    RunInfo run;
    run.source = path.string();
    run.bytes = file.size();
    stats::Aggregator aggregator = aggregate(file.view(), run);
    run.parse_seconds = seconds_since(start);
    Report report = build_report(std::move(aggregator), std::move(run));
    report.run.elapsed_seconds = seconds_since(start);
    return report;
}

Report Analyzer::analyze_stream(std::istream& in, std::string source) const {
    const auto start = Clock::now();
    RunInfo run;
    run.source = std::move(source);
    stats::Aggregator aggregator;
    log::SlowLogParser parser([&](const log::QueryEvent& e) {
        if (accepts(e.database)) {
            aggregator.add(e);
        } else {
            ++run.filtered;
        }
    });
    std::string line;
    while (std::getline(in, line)) {
        run.bytes += line.size() + 1;
        parser.feed(line);
    }
    parser.finish();
    run.lines = parser.counters().lines;
    run.skipped = parser.counters().skipped;
    run.parse_seconds = seconds_since(start);
    Report report = build_report(std::move(aggregator), std::move(run));
    report.run.elapsed_seconds = seconds_since(start);
    return report;
}

}
