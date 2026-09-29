#include <algorithm>
#include <format>

#include "snailtrail/advisor/rules.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

RowsExaminedRatioRule::RowsExaminedRatioRule()
    : Rule({"ST014", "rows-examined-ratio", "Rows examined far exceeding rows returned",
            Severity::Warning, false, false, true}) {}

void RowsExaminedRatioRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const stats::QueryClass& s = *context.stats;
    if (s.calls() == 0) return;
    const double examined = s.rows_examined().mean();
    if (examined < 1000.0) return;

    const bool select = s.kind() == sql::StatementKind::Select;
    const double returned = select ? s.rows_sent().mean() : s.rows_affected().mean();
    const double ratio = examined / std::max(1.0, returned);
    if (ratio < 100.0) return;

    const Severity severity =
        (examined >= 100'000.0 && ratio >= 10'000.0) ? Severity::Critical : Severity::Warning;
    std::string title =
        returned > 0.0
            ? std::format("Examines {} rows to {} {} ({}:1)", util::format_compact(examined),
                          select ? "return" : "change", util::format_compact(returned), util::format_compact(ratio))
            : std::format("Examines {} rows per execution", util::format_compact(examined));
    std::string detail = std::format(
        "Averaged over {} execution(s) in the slow log. A well-indexed lookup examines about as many "
        "rows as it returns.",
        util::format_count(s.calls()));
    if (context.facts != nullptr && context.facts->has_aggregate) {
        detail += " This query aggregates, which reads many rows by design: if it runs often, keep a "
                  "summary table up to date instead.";
    }
    out.push_back(finding(severity, std::move(title), std::move(detail),
                          "The other findings for this query explain the cause when it is visible in the SQL; "
                          "otherwise run EXPLAIN on the worst sample to see which table is scanned."));
}

TempTablesOnDiskRule::TempTablesOnDiskRule()
    : Rule({"ST015", "tmp-tables-on-disk", "Internal temporary tables spilling to disk",
            Severity::Warning, false, false, true}) {}

void TempTablesOnDiskRule::check(const RuleContext& context, std::vector<Finding>& out) const {
    const stats::QueryClass& s = *context.stats;
    const double ratio = s.flag_ratio(log::ExecutionFlag::TmpTableOnDisk);
    if (s.flag_count(log::ExecutionFlag::TmpTableOnDisk) == 0 || ratio < 0.1) return;
    out.push_back(finding(
        Severity::Warning,
        std::format("Temporary tables spill to disk in {:.0f}% of executions", ratio * 100.0),
        "GROUP BY, DISTINCT, UNION and some ORDER BY and subquery plans materialize an internal "
        "temporary table; once it outgrows tmp_table_size it is moved to disk.",
        "Index the GROUP BY / ORDER BY columns so no temporary table is needed, select fewer or "
        "narrower columns, or raise tmp_table_size (and max_heap_table_size)."));
}

}
