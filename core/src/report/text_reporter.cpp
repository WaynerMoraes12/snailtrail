#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "snailtrail/report/reporter.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::report {

namespace {

class Style {
public:
    explicit Style(bool on) : on_(on) {}

    [[nodiscard]] std::string paint(std::string_view code, std::string_view text) const {
        if (!on_ || text.empty()) return std::string(text);
        return std::format("\x1b[{}m{}\x1b[0m", code, text);
    }
    [[nodiscard]] std::string bold(std::string_view t) const { return paint("1", t); }
    [[nodiscard]] std::string dim(std::string_view t) const { return paint("2", t); }
    [[nodiscard]] std::string red(std::string_view t) const { return paint("1;31", t); }
    [[nodiscard]] std::string yellow(std::string_view t) const { return paint("33", t); }
    [[nodiscard]] std::string cyan(std::string_view t) const { return paint("36", t); }
    [[nodiscard]] std::string green(std::string_view t) const { return paint("32", t); }
    [[nodiscard]] std::string magenta(std::string_view t) const { return paint("35", t); }

    [[nodiscard]] std::string severity(advisor::Severity s, std::string_view t) const {
        switch (s) {
        case advisor::Severity::Critical: return red(t);
        case advisor::Severity::Warning: return yellow(t);
        case advisor::Severity::Info: return cyan(t);
        }
        return std::string(t);
    }

private:
    bool on_;
};

std::string_view icon(advisor::Severity s) {
    switch (s) {
    case advisor::Severity::Critical: return "●";
    case advisor::Severity::Warning: return "▲";
    case advisor::Severity::Info: return "○";
    }
    return "○";
}

std::size_t visible_width(std::string_view text) noexcept {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            while (i < text.size() && text[i] != 'm') ++i;
            continue;
        }
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++width;
    }
    return width;
}

std::string pad_right(std::string_view text, std::size_t width) {
    std::string out(text);
    const std::size_t w = visible_width(text);
    if (w < width) out.append(width - w, ' ');
    return out;
}

std::string pad_left(std::string_view text, std::size_t width) {
    const std::size_t w = visible_width(text);
    return w < width ? std::string(width - w, ' ') + std::string(text) : std::string(text);
}

std::vector<std::string> wrap(std::string_view text, std::size_t width) {
    std::vector<std::string> lines;
    width = std::max<std::size_t>(width, 20);
    for (std::string_view paragraph : util::split(text, '\n')) {
        std::string line;
        std::size_t line_width = 0;
        std::size_t pos = 0;
        bool any = false;
        while (pos < paragraph.size()) {
            while (pos < paragraph.size() && paragraph[pos] == ' ') ++pos;
            const std::size_t end = paragraph.find(' ', pos);
            const std::string_view word =
                paragraph.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
            pos = end == std::string_view::npos ? paragraph.size() : end;
            if (word.empty()) continue;
            const std::size_t w = util::display_width(word);
            if (line_width > 0 && line_width + 1 + w > width) {
                lines.push_back(std::move(line));
                line.clear();
                line_width = 0;
            }
            if (line_width > 0) {
                line += ' ';
                ++line_width;
            }
            line += word;
            line_width += w;
            any = true;
        }
        if (any || lines.empty() || !paragraph.empty()) lines.push_back(std::move(line));
    }
    return lines;
}

std::string duration_us(double micros) { return util::format_duration(micros / 1e6); }

std::string percent(double ratio) { return std::format("{:.1f}%", ratio * 100.0); }

std::string rule_line(std::string_view left, std::string_view right, std::size_t width, const Style& style) {
    const std::size_t used = util::display_width(left) + util::display_width(right);
    std::string fill;
    for (std::size_t i = used; i < width; ++i) fill += "─";
    return style.bold(std::string(left)) + style.dim(fill) + style.dim(std::string(right));
}

void print_block(std::ostream& out, std::string_view text, std::size_t indent, std::size_t width,
                 std::size_t max_lines = 0) {
    const std::string pad(indent, ' ');
    const auto lines = wrap(text, width > indent ? width - indent : 20);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (max_lines > 0 && i == max_lines) {
            out << pad << "… (" << (lines.size() - i) << " more lines)\n";
            break;
        }
        out << pad << lines[i] << '\n';
    }
}

std::string tally_text(const stats::Tally& tally, std::uint64_t total) {
    std::vector<std::string> parts;
    for (std::size_t i = 0; i < tally.size() && i < 3; ++i) {
        const auto& [name, n] = tally[i];
        parts.push_back(std::format("{} ({:.0f}%)", name.empty() ? "-" : name,
                                    100.0 * static_cast<double>(n) / static_cast<double>(std::max<std::uint64_t>(total, 1))));
    }
    if (tally.size() > 3) parts.push_back(std::format("+{} more", tally.size() - 3));
    return util::join(parts, ", ");
}

std::string latency_cells(const std::array<std::string, 8>& cells) {
    std::string out;
    for (std::size_t i = 0; i < cells.size(); ++i) out += pad_left(cells[i], i == 0 ? 3 : 6);
    return out;
}

std::string latency_labels() {
    return latency_cells({"1µs", "10µs", "100µs", "1ms", "10ms", "100ms", "1s", "10s+"});
}

std::string sparkline(const std::array<std::uint64_t, 8>& decades) {
    static constexpr const char* bars[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    const std::uint64_t peak = *std::max_element(decades.begin(), decades.end());
    std::array<std::string, 8> cells;
    for (std::size_t i = 0; i < decades.size(); ++i) {
        cells[i] = "·";
        if (decades[i] > 0 && peak > 0) {
            const auto level = static_cast<std::size_t>(
                (static_cast<double>(decades[i]) / static_cast<double>(peak)) * 7.0 + 0.5);
            cells[i] = bars[std::min<std::size_t>(level, 7)];
        }
    }
    return latency_cells(cells);
}

void render_class(const analysis::ClassReport& c, std::ostream& out, const RenderOptions& o, const Style& s) {
    const stats::QueryClass& q = c.stats;
    const std::size_t width = o.width;
    out << '\n'
        << rule_line(std::format("── #{} · {} · {} ", c.rank, q.id_hex(), c.label()),
                     std::format(" {} of query time ──", percent(c.time_share)), width, s)
        << "\n\n";

    auto row = [&](std::string_view label, const std::string& value) {
        out << "  " << s.dim(pad_right(label, 11)) << value << '\n';
    };

    std::string calls = util::format_count(q.calls());
    if (q.last_seen() > q.first_seen()) {
        const double span = static_cast<double>(q.last_seen() - q.first_seen());
        calls += std::format(" · {}/s over {}", util::format_compact(static_cast<double>(q.calls()) / span),
                             util::format_duration(span));
    }
    row("Calls", calls);
    row("Time", std::format("total {} · avg {} · p50 {} · p95 {} · p99 {} · max {}",
                            s.bold(duration_us(static_cast<double>(q.query_time().sum()))),
                            duration_us(q.query_time().mean()), duration_us(static_cast<double>(q.percentile_us(0.5))),
                            duration_us(static_cast<double>(q.percentile_us(0.95))),
                            duration_us(static_cast<double>(q.percentile_us(0.99))),
                            duration_us(static_cast<double>(q.query_time().max()))));
    row("Lock", std::format("avg {} · max {}", duration_us(q.lock_time().mean()),
                            duration_us(static_cast<double>(q.lock_time().max()))));
    const bool select = q.kind() == sql::StatementKind::Select;
    row("Rows", std::format("examined {}/call · {} {}/call", util::format_compact(q.rows_examined().mean()),
                            select ? "sent" : "affected",
                            util::format_compact(select ? q.rows_sent().mean() : q.rows_affected().mean())));

    std::vector<std::string> flags;
    const std::pair<log::ExecutionFlag, std::string_view> names[] = {
        {log::ExecutionFlag::FullScan, "full scan"},
        {log::ExecutionFlag::Filesort, "filesort"},
        {log::ExecutionFlag::TmpTable, "tmp table"},
        {log::ExecutionFlag::TmpTableOnDisk, "tmp table on disk"}};
    for (const auto& [flag, name] : names) {
        if (q.flag_count(flag) > 0) flags.push_back(std::format("{} {:.0f}%", name, q.flag_ratio(flag) * 100.0));
    }
    if (!flags.empty()) row("Execution", util::join(flags, " · "));
    row("Databases", tally_text(q.databases(), q.calls()) + s.dim("   users ") + tally_text(q.users(), q.calls()));
    row("Latency", s.dim(latency_labels()));
    out << "  " << std::string(11, ' ') << s.magenta(sparkline(q.latency().decades())) << "\n\n";

    out << "  " << s.dim("Fingerprint") << '\n';
    print_block(out, q.fingerprint(), 4, width, 6);

    if (!c.findings.empty()) {
        out << '\n';
        render_findings(c.findings, out, o, 2);
    }

    const stats::Sample& w = q.worst();
    out << '\n'
        << "  "
        << s.dim(std::format("Worst sample · {} · {}{}{} · {} rows examined", duration_us(static_cast<double>(w.query_time_us)),
                             w.timestamp != 0 ? util::format_timestamp(w.timestamp) : std::string("time unknown"),
                             w.user.empty() ? "" : " · " + w.user + (w.host.empty() ? "" : "@" + w.host),
                             w.database.empty() ? "" : " · " + w.database, util::format_count(w.rows_examined)))
        << '\n';
    print_block(out, w.sql, 4, width, o.sample_lines);
}

}

void render_findings(std::span<const advisor::Finding> findings, std::ostream& out, const RenderOptions& options,
                     std::size_t indent) {
    const Style s(options.color);
    const std::string pad(indent, ' ');
    const std::size_t text_indent = indent + 2;
    for (const advisor::Finding& f : findings) {
        out << pad << s.severity(f.severity, std::string(icon(f.severity))) << ' '
            << s.severity(f.severity, f.rule_id) << ' ' << s.dim(f.rule_name) << s.dim(" · ") << s.bold(f.title) << '\n';
        if (!f.detail.empty()) {
            for (const auto& line : wrap(f.detail, options.width > text_indent ? options.width - text_indent : 40)) {
                out << std::string(text_indent, ' ') << s.dim(line) << '\n';
            }
        }
        if (!f.suggestion.empty()) {
            bool first = true;
            for (const auto& line : wrap(f.suggestion, options.width > text_indent + 2 ? options.width - text_indent - 2 : 40)) {
                out << std::string(text_indent, ' ') << (first ? s.green("→ ") : "  ") << s.green(line) << '\n';
                first = false;
            }
        }
    }
}

void TextReporter::render(const analysis::Report& report, std::ostream& out) const {
    const Style s(options_.color);
    const auto& run = report.run;
    const auto& t = report.totals;
    const std::size_t width = options_.width;

    out << s.bold(std::format("SnailTrail {}", run.version)) << s.dim(" · ") << run.source << s.dim(" · ")
        << util::format_bytes(run.bytes) << s.dim(" · ") << run.threads
        << (run.threads == 1 ? " thread" : " threads") << s.dim(" · ")
        << util::format_duration(run.elapsed_seconds) << "\n\n";

    const auto counts = report.findings_by_severity();
    const std::string window =
        t.first_seen == 0 ? std::string("-")
                          : std::format("{} → {}", util::format_timestamp(t.first_seen),
                                        util::format_timestamp(t.last_seen).substr(11));
    std::vector<std::pair<std::string, std::string>> left = {
        {"Events", util::format_count(t.events) +
                       (run.skipped + run.filtered > 0
                            ? s.dim(std::format(" ({} skipped)", util::format_count(run.skipped + run.filtered)))
                            : "")},
        {"Query time", s.bold(duration_us(static_cast<double>(t.query_time_us)))},
        {"Lock time", duration_us(static_cast<double>(t.lock_time_us))},
        {"Rows", std::format("{} examined · {} sent", util::format_compact(static_cast<double>(t.rows_examined)),
                             util::format_compact(static_cast<double>(t.rows_sent)))},
    };
    std::vector<std::pair<std::string, std::string>> right = {
        {"Window", window},
        {"Classes", util::format_count(report.class_count) +
                        (report.classes.size() < report.class_count
                             ? s.dim(std::format(" (top {})", report.classes.size()))
                             : "")},
        {"Findings", std::format("{} {}  {} {}  {} {}", s.red("●"), counts[2], s.yellow("▲"), counts[1],
                                 s.cyan("○"), counts[0])},
        {"Schema", run.schema_tables > 0 ? std::format("{} tables", run.schema_tables) : s.dim("not loaded")},
    };
    for (std::size_t i = 0; i < left.size(); ++i) {
        out << "  " << s.dim(pad_right(left[i].first, 11)) << pad_right(left[i].second, 34) << "  "
            << s.dim(pad_right(right[i].first, 10)) << right[i].second << '\n';
    }

    if (report.classes.empty()) {
        out << "\n  No query events found.\n";
        return;
    }

    const std::size_t fixed = 2 + 3 + 8 + 7 + 6 + 6 + 7 + 7 + 6 + 4 + 2 * 9;
    const std::size_t label_width = width > fixed + 10 ? width - fixed : 10;
    out << '\n'
        << s.dim(std::format("  {}  {}  {}  {}  {}  {}  {}  {}  {}  {}", pad_left("#", 3), pad_right("Query ID", 8),
                             pad_left("Time", 7), pad_left("Share", 6), pad_left("Calls", 6), pad_left("Avg", 7),
                             pad_left("p95", 7), pad_left("Rows", 6), pad_right("Adv", 4), "Query"))
        << '\n';
    for (const auto& c : report.classes) {
        const auto& q = c.stats;
        std::string advice;
        if (const auto worst = c.worst_severity()) {
            advice = s.severity(*worst, std::format("{}{}", icon(*worst), c.findings.size()));
        }
        out << "  " << pad_left(std::to_string(c.rank), 3) << "  " << s.dim(q.id_hex().substr(0, 8)) << "  "
            << pad_left(duration_us(static_cast<double>(q.query_time().sum())), 7) << "  "
            << pad_left(percent(c.time_share), 6) << "  "
            << pad_left(util::format_compact(static_cast<double>(q.calls())), 6) << "  "
            << pad_left(duration_us(q.query_time().mean()), 7) << "  "
            << pad_left(duration_us(static_cast<double>(q.percentile_us(0.95))), 7) << "  "
            << pad_left(util::format_compact(q.rows_examined().mean()), 6) << "  "
            << pad_right(advice, 4) << "  "
            << util::abbreviate(c.label(), label_width) << '\n';
    }

    const std::size_t details = std::min(options_.detail_limit, report.classes.size());
    for (std::size_t i = 0; i < details; ++i) render_class(report.classes[i], out, options_, s);

    out << '\n';
    const double mb_per_s = run.parse_seconds > 0 ? static_cast<double>(run.bytes) / run.parse_seconds : 0.0;
    const double events_per_s = run.parse_seconds > 0 ? static_cast<double>(t.events) / run.parse_seconds : 0.0;
    out << s.dim(std::format("Parsed {} and {} events in {} ({}/s, {} events/s) with {} {}.",
                             util::format_bytes(run.bytes), util::format_count(t.events),
                             util::format_duration(run.parse_seconds),
                             util::format_bytes(static_cast<std::uint64_t>(mb_per_s)),
                             util::format_compact(events_per_s), run.threads,
                             run.threads == 1 ? "thread" : "threads"))
        << '\n';
    if (details < report.classes.size() || report.classes.size() < report.class_count) {
        out << s.dim(std::format("Detailed {} of {} query classes; use --top and --details to see more.", details,
                                 report.class_count))
            << '\n';
    }
}

}
