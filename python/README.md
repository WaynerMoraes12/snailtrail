# python/

The `snailtrail` Python package: the C++20 engine as a native extension, plus the
MySQL-backed dashboard and the lab workload built on it.

```
python/
├── snailtrail/
│   ├── __init__.py       the public API, re-exported from the extension
│   ├── _native.pyi       type stubs for the extension (the module itself is built from bindings/python)
│   ├── py.typed          PEP 561 marker: the package ships its types
│   ├── dashboard/        FastAPI app, MySQL history, EXPLAIN, schema discovery
│   └── lab/              seeds the shop database and replays a realistic workload
└── tests/                pytest suite
```

## Install

The package is built by [scikit-build-core](https://scikit-build-core.readthedocs.io):
`pip` runs CMake, compiles the C++ core and the pybind11 module, and puts the extension
next to the Python files.

```bash
pip install .                    # the engine only, no dependencies
pip install ".[dashboard,lab]"   # plus the web dashboard and the lab
pip install ".[dev]"             # plus pytest, httpx, ruff
```

A C++20 compiler and CMake 3.21+ must be available (the Docker images have them).

## The engine from Python

```python
import snailtrail

schema = snailtrail.SchemaCatalog.from_ddl(open("schema.sql").read())
report = snailtrail.analyze_file("/var/log/mysql/slow.log", schema=schema, top=20)

for c in report.classes:
    print(f"#{c.rank} {c.label:40} {c.total_time_us / 1e6:8.1f}s  p95 {c.p95_us / 1e3:.0f} ms")
    for f in c.findings:
        print(f"   {f.severity:8} {f.rule_id} {f.title}")
        print(f"            {f.suggestion}")

open("report.md", "w").write(report.render("markdown"))
```

| Function | Returns |
|---|---|
| `analyze_file(path, *, threads, top, sort, schema, database, min_severity, advise, disable)` | `Report` |
| `analyze_text(text, ...)` | `Report` |
| `advise(sql, schema=None, disable=())` | `list[Finding]` |
| `fingerprint(sql)` | `Fingerprint` (`id`, `kind`, `text`) |
| `parse_tree(sql)` | the syntax tree as text; raises `ParseError` (a `ValueError` with `.offset`) |
| `rules()` | the rule catalog |
| `generate_log(events, seed, extra_fields)` | a synthetic MySQL 8.4 slow log |
| `SchemaCatalog.from_ddl(ddl)` | a schema catalog (`tables`, `indexes()`, `warnings`) |

`Report` exposes the run (`events`, `bytes`, `threads`, `elapsed_seconds`...), `totals`,
`findings_by_severity`, the ranked `classes` and `render("text" | "json" | "markdown")`.
Each `QueryClass` carries its metrics (`calls`, `total_time_us`, `p50_us`/`p95_us`/`p99_us`,
`rows_examined`, execution `flags`, `latency_decades`...), its worst `sample` and its
`findings`.

### Threads and the GIL

`analyze_file`, `analyze_text` and `generate_log` release the GIL for the whole analysis:
other Python threads keep running while the C++ workers parse, and a web server can serve
requests while an analysis is in progress. `test_analysis_releases_the_gil` checks it.

### Errors

| Situation | Python exception |
|---|---|
| unparseable SQL in `parse_tree` | `snailtrail.ParseError` (subclass of `ValueError`), with `.offset` |
| unknown rule, sort key, severity or format | `ValueError` |
| missing or unreadable log file | `OSError` |

## Tests

```bash
pytest                         # the engine, the dashboard with fakes, the lab's scenarios
SNAILTRAIL_TEST_MYSQL_DSN=mysql://root:snailtrail@127.0.0.1:3307/snailtrail_test pytest -m mysql
```

Tests marked `mysql` run against a real server (the Docker lab works) and are skipped
otherwise.
