# core/src/report

Rendering a `Report` for people, for programs and for pull requests.

| File | What it does |
|---|---|
| `reporter.cpp` | the factory: `make_reporter("text" \| "json" \| "markdown")` |
| `text_reporter.cpp` | the terminal report |
| `json_reporter.cpp` | the machine-readable report (used by CI and the Python side) |
| `markdown_reporter.cpp` | a report for PR comments and CI job summaries |

`Reporter` is a **strategy**: one abstract `render(report, ostream)`, three
implementations, chosen by name at run time. Adding a format (HTML, CSV) touches nothing
else.

## Text

```
SnailTrail 1.0.0 · slow.log · 13.0 MB · 13 threads · 30.2 ms

  Events     19,935 (65 skipped)                 Window    2024-01-15 10:00:00 → 10:06:44
  Query time 21m 59s                             Classes   20 (top 12)
  Lock time  130 ms                              Findings  ● 14  ▲ 14  ○ 0
  Rows       2.7B examined · 237k sent           Schema    6 tables

    #  Query ID     Time   Share   Calls      Avg      p95    Rows  Adv   Query
    1  E9EF4339   5m 02s   22.9%    1.6k   191 ms   315 ms    483k  ●2    SELECT order_items, produ…
    2  51459E22   2m 29s   11.3%    1.2k   119 ms   190 ms    200k  ●2    SELECT customers
```

then, for the top classes, a detail block: calls and rate, time percentiles, lock time,
rows per call, execution flags (full scan, filesort, temporary tables), databases and
users, a **latency sparkline** with one cell per decade, the fingerprint, the findings
with their fix, and the worst sample.

- **Colours** are ANSI escapes, on when writing to a terminal (`--color` / `--no-color`,
  and the `NO_COLOR` convention). Padding measures *visible* width, skipping escape
  sequences and counting UTF-8 code points, so columns stay aligned in colour.
- **Wrapping** is word-based at the requested width; long findings and samples indent
  under their label.
- The **Rows** column is rows examined per execution — the single number that best
  separates an indexed lookup from a scan.

## JSON

A stable schema for tools:

```json
{
  "tool": {"name": "snailtrail", "version": "1.0.0"},
  "run": {"source": "slow.log", "bytes": 13631488, "threads": 13, "elapsed_seconds": 0.03, ...},
  "totals": {"events": 19935, "query_time_us": 1319021554, "rows_examined": ..., ...},
  "sort": "time", "class_count": 20,
  "findings": {"critical": 14, "warning": 14, "info": 0},
  "classes": [{
    "rank": 1, "id": "E9EF43398DCC888A", "kind": "SELECT", "fingerprint": "select ...",
    "tables": ["order_items", "products"], "calls": 1582, "time_share": 0.229,
    "query_time_us": {"sum": ..., "avg": ..., "min": ..., "max": ..., "p50": ..., "p95": ..., "p99": ...},
    "lock_time_us": {...}, "rows_sent": {...}, "rows_examined": {...}, "rows_affected": {...},
    "flags": {"full_scan": 1582, "filesort": 0, "tmp_table": 0, "tmp_table_on_disk": 0},
    "latency_decades": [0, 0, 0, 0, 12, 1570, 0, 0],
    "databases": [{"name": "shop", "count": 1582}], "users": [...],
    "sample": {"sql": "SELECT ...", "query_time_us": 505000, ...},
    "findings": [{"rule_id": "ST001", "rule_name": "missing-index", "severity": "critical",
                  "title": "...", "detail": "...", "suggestion": "ALTER TABLE ..."}]
  }]
}
```

Times are integer microseconds, exactly as aggregated; ratios are plain doubles.

## Markdown

A summary table, the top-queries table (each row links to its section), and for each
detailed class: a metrics table, the fingerprint and every fix in `sql` code blocks, and
the worst sample folded in a `<details>` element — ready to paste into a pull request or
to append to `$GITHUB_STEP_SUMMARY`.
