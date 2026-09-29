#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "snailtrail/advisor/rule_engine.hpp"
#include "snailtrail/analysis/analyzer.hpp"
#include "snailtrail/log/generator.hpp"
#include "snailtrail/report/reporter.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/sql/ast_printer.hpp"
#include "snailtrail/sql/fingerprint.hpp"
#include "snailtrail/sql/parser.hpp"
#include "snailtrail/util/strings.hpp"

namespace py = pybind11;
using namespace snailtrail;

namespace {

struct Catalog {
    schema::SchemaCatalog catalog;
    std::vector<std::string> warnings;
};

py::dict finding_dict(const advisor::Finding& f) {
    py::dict d;
    d["rule_id"] = f.rule_id;
    d["rule_name"] = f.rule_name;
    d["severity"] = std::string(advisor::severity_name(f.severity));
    d["title"] = f.title;
    d["detail"] = f.detail;
    d["suggestion"] = f.suggestion;
    return d;
}

advisor::RuleEngine engine_without(const std::vector<std::string>& disabled) {
    auto engine = advisor::RuleEngine::with_default_rules();
    for (const auto& id : disabled) {
        if (!engine.disable(id)) throw py::value_error("unknown rule '" + id + "'");
    }
    return engine;
}

advisor::Severity severity_from(const std::string& name) {
    const auto s = advisor::parse_severity(name);
    if (!s) throw py::value_error("severity must be 'info', 'warning' or 'critical'");
    return *s;
}

analysis::SortKey sort_from(const std::string& name) {
    const auto k = analysis::parse_sort_key(name);
    if (!k) throw py::value_error("sort must be one of time, calls, avg, p95, max, rows");
    return *k;
}

struct AnalyzeArgs {
    unsigned threads;
    std::size_t top;
    std::string sort;
    const Catalog* schema;
    std::optional<std::string> database;
    std::string min_severity;
    bool advise;
    std::vector<std::string> disable;
};

template <typename Run>
analysis::Report analyze_with(const AnalyzeArgs& a, Run&& run) {
    analysis::AnalyzeOptions options;
    options.threads = a.threads;
    options.top = a.top;
    options.sort = sort_from(a.sort);
    options.database = a.database.value_or("");
    options.min_severity = severity_from(a.min_severity);
    options.advise = a.advise;
    const advisor::RuleEngine engine = engine_without(a.disable);
    const schema::SchemaCatalog* catalog = a.schema == nullptr ? nullptr : &a.schema->catalog;
    py::gil_scoped_release release;
    const analysis::Analyzer analyzer(options, catalog, &engine);
    return run(analyzer);
}

std::string render(const analysis::Report& report, const std::string& format, bool color, std::size_t width,
                   std::size_t details) {
    report::RenderOptions options;
    options.color = color;
    options.width = width;
    options.detail_limit = details;
    std::ostringstream out;
    report::make_reporter(format, options)->render(report, out);
    return out.str();
}

}

PYBIND11_MODULE(_native, m) {
    m.doc() = "SnailTrail: a MySQL slow-log analyzer and index advisor (C++20 core)";
    m.attr("__version__") = SNAILTRAIL_VERSION;

    static py::exception<sql::ParseError> parse_error(m, "ParseError", PyExc_ValueError);

    py::register_exception_translator([](std::exception_ptr error) {
        try {
            if (error) std::rethrow_exception(error);
        } catch (const std::system_error& e) {
            PyErr_SetString(PyExc_OSError, e.what());
        }
    });

    py::class_<advisor::Finding>(m, "Finding")
        .def_readonly("rule_id", &advisor::Finding::rule_id)
        .def_readonly("rule_name", &advisor::Finding::rule_name)
        .def_property_readonly("severity",
                               [](const advisor::Finding& f) { return std::string(advisor::severity_name(f.severity)); })
        .def_readonly("title", &advisor::Finding::title)
        .def_readonly("detail", &advisor::Finding::detail)
        .def_readonly("suggestion", &advisor::Finding::suggestion)
        .def("to_dict", &finding_dict)
        .def("__repr__", [](const advisor::Finding& f) {
            return "<Finding " + f.rule_id + " " + std::string(advisor::severity_name(f.severity)) + ": " + f.title + ">";
        });

    py::class_<sql::Fingerprint>(m, "Fingerprint")
        .def_property_readonly("id", &sql::Fingerprint::id_hex)
        .def_property_readonly("kind", [](const sql::Fingerprint& f) { return std::string(sql::statement_kind_name(f.kind)); })
        .def_readonly("text", &sql::Fingerprint::text)
        .def("__repr__", [](const sql::Fingerprint& f) { return "<Fingerprint " + f.id_hex() + " " + f.text + ">"; });

    m.def("fingerprint", &sql::fingerprint, py::arg("sql"), "Normalize a statement into its query-class fingerprint");

    m.def(
        "parse_tree",
        [](const std::string& sql) {
            try {
                return sql::dump_ast(*sql::parse(sql));
            } catch (const sql::ParseError& e) {
                const py::object type = parse_error;
                py::object instance = type(e.what());
                instance.attr("offset") = e.offset();
                PyErr_SetObject(type.ptr(), instance.ptr());
                throw py::error_already_set();
            }
        },
        py::arg("sql"), "Parse a statement and draw its syntax tree");

    py::class_<Catalog, std::shared_ptr<Catalog>>(m, "SchemaCatalog")
        .def_static(
            "from_ddl",
            [](const std::string& ddl) {
                auto c = std::make_shared<Catalog>();
                c->catalog = schema::SchemaCatalog::from_ddl(ddl, &c->warnings);
                return c;
            },
            py::arg("ddl"), "Load CREATE TABLE / ALTER TABLE / CREATE INDEX statements")
        .def_property_readonly("tables",
                               [](const Catalog& c) {
                                   std::vector<std::string> names;
                                   for (const auto* t : c.catalog.tables()) names.push_back(t->name());
                                   return names;
                               })
        .def_readonly("warnings", &Catalog::warnings)
        .def("has_table", [](const Catalog& c, const std::string& name) { return c.catalog.find_table(name) != nullptr; })
        .def("indexes",
             [](const Catalog& c, const std::string& table) {
                 std::vector<std::pair<std::string, std::vector<std::string>>> out;
                 if (const auto* t = c.catalog.find_table(table)) {
                     for (const auto& i : t->indexes()) out.emplace_back(i.name, i.columns);
                 }
                 return out;
             })
        .def("__len__", [](const Catalog& c) { return c.catalog.size(); })
        .def("__repr__", [](const Catalog& c) { return "<SchemaCatalog " + std::to_string(c.catalog.size()) + " tables>"; });

    m.def(
        "rules",
        []() {
            py::list out;
            const advisor::RuleEngine engine = advisor::RuleEngine::with_default_rules();
            for (const advisor::Rule* r : engine.rules()) {
                const auto& i = r->info();
                py::dict d;
                d["id"] = std::string(i.id);
                d["name"] = std::string(i.name);
                d["summary"] = std::string(i.summary);
                d["severity"] = std::string(advisor::severity_name(i.severity));
                d["needs_schema"] = i.needs_schema;
                d["needs_stats"] = i.needs_stats;
                out.append(d);
            }
            return out;
        },
        "Describe the advisor rules");

    m.def(
        "advise",
        [](const std::string& sql, const std::shared_ptr<Catalog>& schema, const std::vector<std::string>& disable) {
            const advisor::RuleEngine engine = engine_without(disable);
            return engine.advise(sql, schema ? &schema->catalog : nullptr);
        },
        py::arg("sql"), py::arg("schema") = nullptr, py::arg("disable") = std::vector<std::string>{},
        "Check one statement against the advisor rules");

    py::class_<analysis::ClassReport>(m, "QueryClass")
        .def_readonly("rank", &analysis::ClassReport::rank)
        .def_property_readonly("id", [](const analysis::ClassReport& c) { return c.stats.id_hex(); })
        .def_property_readonly("kind", [](const analysis::ClassReport& c) { return std::string(sql::statement_kind_name(c.stats.kind())); })
        .def_property_readonly("fingerprint", [](const analysis::ClassReport& c) { return c.stats.fingerprint(); })
        .def_property_readonly("label", &analysis::ClassReport::label)
        .def_readonly("tables", &analysis::ClassReport::tables)
        .def_readonly("time_share", &analysis::ClassReport::time_share)
        .def_readonly("findings", &analysis::ClassReport::findings)
        .def_property_readonly("calls", [](const analysis::ClassReport& c) { return c.stats.calls(); })
        .def_property_readonly("total_time_us", [](const analysis::ClassReport& c) { return c.stats.query_time().sum(); })
        .def_property_readonly("avg_time_us", [](const analysis::ClassReport& c) { return c.stats.query_time().mean(); })
        .def_property_readonly("min_time_us", [](const analysis::ClassReport& c) { return c.stats.query_time().min(); })
        .def_property_readonly("max_time_us", [](const analysis::ClassReport& c) { return c.stats.query_time().max(); })
        .def_property_readonly("p50_us", [](const analysis::ClassReport& c) { return c.stats.percentile_us(0.50); })
        .def_property_readonly("p95_us", [](const analysis::ClassReport& c) { return c.stats.percentile_us(0.95); })
        .def_property_readonly("p99_us", [](const analysis::ClassReport& c) { return c.stats.percentile_us(0.99); })
        .def_property_readonly("lock_time_us", [](const analysis::ClassReport& c) { return c.stats.lock_time().sum(); })
        .def_property_readonly("rows_sent", [](const analysis::ClassReport& c) { return c.stats.rows_sent().sum(); })
        .def_property_readonly("rows_examined", [](const analysis::ClassReport& c) { return c.stats.rows_examined().sum(); })
        .def_property_readonly("rows_affected", [](const analysis::ClassReport& c) { return c.stats.rows_affected().sum(); })
        .def_property_readonly("first_seen", [](const analysis::ClassReport& c) { return c.stats.first_seen(); })
        .def_property_readonly("last_seen", [](const analysis::ClassReport& c) { return c.stats.last_seen(); })
        .def_property_readonly("latency_decades", [](const analysis::ClassReport& c) { return c.stats.latency().decades(); })
        .def_property_readonly("databases", [](const analysis::ClassReport& c) { return c.stats.databases(); })
        .def_property_readonly("flags",
                               [](const analysis::ClassReport& c) {
                                   py::dict d;
                                   d["full_scan"] = c.stats.flag_ratio(log::ExecutionFlag::FullScan);
                                   d["filesort"] = c.stats.flag_ratio(log::ExecutionFlag::Filesort);
                                   d["tmp_table"] = c.stats.flag_ratio(log::ExecutionFlag::TmpTable);
                                   d["tmp_table_on_disk"] = c.stats.flag_ratio(log::ExecutionFlag::TmpTableOnDisk);
                                   return d;
                               })
        .def_property_readonly("sample",
                               [](const analysis::ClassReport& c) {
                                   const auto& s = c.stats.worst();
                                   py::dict d;
                                   d["sql"] = s.sql;
                                   d["database"] = s.database;
                                   d["user"] = s.user;
                                   d["host"] = s.host;
                                   d["query_time_us"] = s.query_time_us;
                                   d["rows_examined"] = s.rows_examined;
                                   d["rows_sent"] = s.rows_sent;
                                   d["timestamp"] = s.timestamp;
                                   return d;
                               })
        .def("__repr__", [](const analysis::ClassReport& c) {
            return "<QueryClass #" + std::to_string(c.rank) + " " + c.stats.id_hex() + " " + c.label() + ">";
        });

    py::class_<analysis::Report>(m, "Report")
        .def_property_readonly("source", [](const analysis::Report& r) { return r.run.source; })
        .def_property_readonly("bytes", [](const analysis::Report& r) { return r.run.bytes; })
        .def_property_readonly("events", [](const analysis::Report& r) { return r.totals.events; })
        .def_property_readonly("skipped", [](const analysis::Report& r) { return r.run.skipped; })
        .def_property_readonly("filtered", [](const analysis::Report& r) { return r.run.filtered; })
        .def_property_readonly("threads", [](const analysis::Report& r) { return r.run.threads; })
        .def_property_readonly("chunks", [](const analysis::Report& r) { return r.run.chunks; })
        .def_property_readonly("parse_seconds", [](const analysis::Report& r) { return r.run.parse_seconds; })
        .def_property_readonly("elapsed_seconds", [](const analysis::Report& r) { return r.run.elapsed_seconds; })
        .def_property_readonly("schema_tables", [](const analysis::Report& r) { return r.run.schema_tables; })
        .def_readonly("class_count", &analysis::Report::class_count)
        .def_readonly("classes", &analysis::Report::classes)
        .def_property_readonly("totals",
                               [](const analysis::Report& r) {
                                   py::dict d;
                                   d["events"] = r.totals.events;
                                   d["query_time_us"] = r.totals.query_time_us;
                                   d["lock_time_us"] = r.totals.lock_time_us;
                                   d["rows_sent"] = r.totals.rows_sent;
                                   d["rows_examined"] = r.totals.rows_examined;
                                   d["rows_affected"] = r.totals.rows_affected;
                                   d["first_seen"] = r.totals.first_seen;
                                   d["last_seen"] = r.totals.last_seen;
                                   return d;
                               })
        .def_property_readonly("findings_by_severity",
                               [](const analysis::Report& r) {
                                   const auto c = r.findings_by_severity();
                                   py::dict d;
                                   d["critical"] = c[2];
                                   d["warning"] = c[1];
                                   d["info"] = c[0];
                                   return d;
                               })
        .def("render", &render, py::arg("format") = "text", py::arg("color") = false, py::arg("width") = 100,
             py::arg("details") = 10, "Render as text, json or markdown")
        .def("__repr__", [](const analysis::Report& r) {
            return "<Report " + r.run.source + ": " + std::to_string(r.totals.events) + " events, " +
                   std::to_string(r.class_count) + " classes>";
        });

    m.def(
        "analyze_file",
        [](const std::string& path, unsigned threads, std::size_t top, const std::string& sort,
           const std::shared_ptr<Catalog>& schema, std::optional<std::string> database,
           const std::string& min_severity, bool advise, const std::vector<std::string>& disable) {
            const AnalyzeArgs a{threads, top, sort, schema.get(), std::move(database), min_severity, advise, disable};
            return analyze_with(a, [&](const analysis::Analyzer& an) { return an.analyze_file(path); });
        },
        py::arg("path"), py::kw_only(), py::arg("threads") = 0U, py::arg("top") = std::size_t{0},
        py::arg("sort") = "time", py::arg("schema") = nullptr, py::arg("database") = py::none(),
        py::arg("min_severity") = "info", py::arg("advise") = true,
        py::arg("disable") = std::vector<std::string>{}, "Analyze a slow log file (releases the GIL)");

    m.def(
        "analyze_text",
        [](const std::string& text, unsigned threads, std::size_t top, const std::string& sort,
           const std::shared_ptr<Catalog>& schema, std::optional<std::string> database,
           const std::string& min_severity, bool advise, const std::vector<std::string>& disable) {
            const AnalyzeArgs a{threads, top, sort, schema.get(), std::move(database), min_severity, advise, disable};
            return analyze_with(a, [&](const analysis::Analyzer& an) { return an.analyze_text(text, "<text>"); });
        },
        py::arg("text"), py::kw_only(), py::arg("threads") = 0U, py::arg("top") = std::size_t{0},
        py::arg("sort") = "time", py::arg("schema") = nullptr, py::arg("database") = py::none(),
        py::arg("min_severity") = "info", py::arg("advise") = true,
        py::arg("disable") = std::vector<std::string>{}, "Analyze slow-log text (releases the GIL)");

    m.def(
        "generate_log",
        [](std::uint64_t events, std::uint64_t seed, bool extra_fields) {
            log::GeneratorOptions o;
            o.events = events;
            o.seed = seed;
            o.extra_fields = extra_fields;
            py::gil_scoped_release release;
            return log::SlowLogGenerator(o).generate();
        },
        py::arg("events") = 10'000, py::arg("seed") = 42, py::arg("extra_fields") = true,
        "Write a synthetic MySQL 8.4 slow log");
}
