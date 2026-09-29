#include "snailtrail/report/reporter.hpp"
#include "snailtrail/util/json_writer.hpp"

namespace snailtrail::report {

namespace {

using util::JsonWriter;

void write_summary(JsonWriter& w, std::string_view name, const stats::Summary<std::uint64_t>& s) {
    w.key(name).begin_object();
    w.field("sum", s.sum()).field("avg", s.mean()).field("min", s.min()).field("max", s.max());
    w.end_object();
}

void write_tally(JsonWriter& w, std::string_view name, const stats::Tally& tally) {
    w.key(name).begin_array();
    for (const auto& [value, count] : tally) {
        w.begin_object().field("name", value).field("count", count).end_object();
    }
    w.end_array();
}

void write_finding(JsonWriter& w, const advisor::Finding& f) {
    w.begin_object()
        .field("rule_id", f.rule_id)
        .field("rule_name", f.rule_name)
        .field("severity", advisor::severity_name(f.severity))
        .field("title", f.title)
        .field("detail", f.detail)
        .field("suggestion", f.suggestion)
        .end_object();
}

void write_class(JsonWriter& w, const analysis::ClassReport& c) {
    const stats::QueryClass& q = c.stats;
    w.begin_object();
    w.field("rank", c.rank)
        .field("id", q.id_hex())
        .field("kind", sql::statement_kind_name(q.kind()))
        .field("fingerprint", q.fingerprint())
        .field("label", c.label());
    w.key("tables").begin_array();
    for (const auto& t : c.tables) w.value(t);
    w.end_array();
    w.field("calls", q.calls()).field("time_share", c.time_share);

    w.key("query_time_us").begin_object();
    w.field("sum", q.query_time().sum())
        .field("avg", q.query_time().mean())
        .field("min", q.query_time().min())
        .field("max", q.query_time().max())
        .field("p50", q.percentile_us(0.50))
        .field("p95", q.percentile_us(0.95))
        .field("p99", q.percentile_us(0.99));
    w.end_object();
    write_summary(w, "lock_time_us", q.lock_time());
    write_summary(w, "rows_sent", q.rows_sent());
    write_summary(w, "rows_examined", q.rows_examined());
    write_summary(w, "rows_affected", q.rows_affected());
    w.field("bytes_sent", q.bytes_sent());

    w.key("flags").begin_object();
    w.field("full_scan", q.flag_count(log::ExecutionFlag::FullScan))
        .field("filesort", q.flag_count(log::ExecutionFlag::Filesort))
        .field("tmp_table", q.flag_count(log::ExecutionFlag::TmpTable))
        .field("tmp_table_on_disk", q.flag_count(log::ExecutionFlag::TmpTableOnDisk));
    w.end_object();

    w.key("latency_decades").begin_array();
    for (auto n : q.latency().decades()) w.value(n);
    w.end_array();
    w.field("first_seen", q.first_seen()).field("last_seen", q.last_seen());
    write_tally(w, "databases", q.databases());
    write_tally(w, "users", q.users());

    const stats::Sample& s = q.worst();
    w.key("sample").begin_object();
    w.field("sql", s.sql)
        .field("database", s.database)
        .field("user", s.user)
        .field("host", s.host)
        .field("query_time_us", s.query_time_us)
        .field("rows_examined", s.rows_examined)
        .field("rows_sent", s.rows_sent)
        .field("timestamp", s.timestamp);
    w.end_object();

    w.key("findings").begin_array();
    for (const auto& f : c.findings) write_finding(w, f);
    w.end_array();
    w.end_object();
}

}

void JsonReporter::render(const analysis::Report& report, std::ostream& out) const {
    JsonWriter w(out, indent_);
    const auto& run = report.run;
    const auto& t = report.totals;
    w.begin_object();
    w.key("tool").begin_object().field("name", "snailtrail").field("version", run.version).end_object();

    w.key("run").begin_object();
    w.field("source", run.source)
        .field("bytes", run.bytes)
        .field("lines", run.lines)
        .field("skipped", run.skipped)
        .field("filtered", run.filtered)
        .field("threads", run.threads)
        .field("chunks", run.chunks)
        .field("parse_seconds", run.parse_seconds)
        .field("elapsed_seconds", run.elapsed_seconds)
        .field("schema_tables", run.schema_tables)
        .field("database_filter", run.database_filter);
    w.end_object();

    w.key("totals").begin_object();
    w.field("events", t.events)
        .field("query_time_us", t.query_time_us)
        .field("lock_time_us", t.lock_time_us)
        .field("rows_sent", t.rows_sent)
        .field("rows_examined", t.rows_examined)
        .field("rows_affected", t.rows_affected)
        .field("first_seen", t.first_seen)
        .field("last_seen", t.last_seen);
    w.end_object();

    const auto counts = report.findings_by_severity();
    w.field("sort", analysis::sort_key_name(report.sort)).field("class_count", report.class_count);
    w.key("findings").begin_object();
    w.field("critical", counts[2]).field("warning", counts[1]).field("info", counts[0]);
    w.end_object();

    w.key("classes").begin_array();
    for (const auto& c : report.classes) write_class(w, c);
    w.end_array();
    w.end_object();
}

void write_findings_json(std::span<const advisor::Finding> findings, std::ostream& out, int indent) {
    JsonWriter w(out, indent);
    w.begin_array();
    for (const auto& f : findings) write_finding(w, f);
    w.end_array();
}

}
