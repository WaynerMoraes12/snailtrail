#include <format>
#include <string>

#include "snailtrail/report/reporter.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::report {

namespace {

std::string cell(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '|') {
            out += "\\|";
        } else if (c == '\n' || c == '\r') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string_view badge(advisor::Severity s) {
    switch (s) {
    case advisor::Severity::Critical: return "🔴";
    case advisor::Severity::Warning: return "🟠";
    case advisor::Severity::Info: return "🔵";
    }
    return "🔵";
}

std::string duration_us(double micros) { return util::format_duration(micros / 1e6); }

}

void MarkdownReporter::render(const analysis::Report& report, std::ostream& out) const {
    const auto& run = report.run;
    const auto& t = report.totals;
    const auto counts = report.findings_by_severity();

    out << "# 🐌 SnailTrail report\n\n";
    out << std::format("`{}` · {} · {} events · {} query classes · analysed in {} by SnailTrail {}\n\n", run.source,
                       util::format_bytes(run.bytes), util::format_count(t.events),
                       util::format_count(report.class_count), util::format_duration(run.elapsed_seconds),
                       run.version);

    out << "| Total query time | Lock time | Rows examined | Rows sent | Findings |\n";
    out << "|---:|---:|---:|---:|:---|\n";
    out << std::format("| {} | {} | {} | {} | 🔴 {} · 🟠 {} · 🔵 {} |\n\n",
                       duration_us(static_cast<double>(t.query_time_us)),
                       duration_us(static_cast<double>(t.lock_time_us)),
                       util::format_compact(static_cast<double>(t.rows_examined)),
                       util::format_compact(static_cast<double>(t.rows_sent)), counts[2], counts[1], counts[0]);

    if (report.classes.empty()) {
        out << "No query events found.\n";
        return;
    }

    out << "## Top queries\n\n";
    out << "| # | Query | Time | Share | Calls | Avg | p95 | Rows examined/call | Findings |\n";
    out << "|---:|:---|---:|---:|---:|---:|---:|---:|:---|\n";
    for (const auto& c : report.classes) {
        const auto& q = c.stats;
        std::string findings;
        for (const auto& f : c.findings) findings += std::string(badge(f.severity)) + " " + f.rule_id + " ";
        out << std::format("| {} | [{}](#{}) `{}` | {} | {:.1f}% | {} | {} | {} | {} | {} |\n", c.rank,
                           cell(c.label()), util::to_lower(q.id_hex()), q.id_hex().substr(0, 8),
                           duration_us(static_cast<double>(q.query_time().sum())), c.time_share * 100.0,
                           util::format_count(q.calls()), duration_us(q.query_time().mean()),
                           duration_us(static_cast<double>(q.percentile_us(0.95))),
                           util::format_compact(q.rows_examined().mean()), util::trim(findings));
    }

    const std::size_t details = std::min(options_.detail_limit, report.classes.size());
    for (std::size_t i = 0; i < details; ++i) {
        const auto& c = report.classes[i];
        const auto& q = c.stats;
        out << std::format("\n## {}\n\n", q.id_hex());
        out << std::format("**#{} · {}** · {:.1f}% of total query time · {} calls\n\n", c.rank, c.label(),
                           c.time_share * 100.0, util::format_count(q.calls()));
        out << "| avg | p50 | p95 | p99 | max | rows examined/call | rows sent/call |\n";
        out << "|---:|---:|---:|---:|---:|---:|---:|\n";
        out << std::format("| {} | {} | {} | {} | {} | {} | {} |\n\n", duration_us(q.query_time().mean()),
                           duration_us(static_cast<double>(q.percentile_us(0.5))),
                           duration_us(static_cast<double>(q.percentile_us(0.95))),
                           duration_us(static_cast<double>(q.percentile_us(0.99))),
                           duration_us(static_cast<double>(q.query_time().max())),
                           util::format_compact(q.rows_examined().mean()), util::format_compact(q.rows_sent().mean()));
        out << "```sql\n" << q.fingerprint() << "\n```\n";
        for (const auto& f : c.findings) {
            out << std::format("\n{} **{} {}** — {}\n\n", badge(f.severity), f.rule_id, f.rule_name, f.title);
            if (!f.detail.empty()) out << f.detail << "\n";
            if (!f.suggestion.empty()) out << "\n```sql\n" << f.suggestion << "\n```\n";
        }
        out << "\n<details><summary>Worst sample ("
            << duration_us(static_cast<double>(q.worst().query_time_us)) << ")</summary>\n\n```sql\n"
            << q.worst().sql << "\n```\n\n</details>\n";
    }
}

}
