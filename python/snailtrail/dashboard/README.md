# snailtrail.dashboard

A web dashboard that analyses the slow log on demand or on a schedule, keeps every run in
MySQL, and shows where the query time goes, what changed since the previous run, and how to
fix each query — with the real `EXPLAIN` plan next to the advice.

```bash
pip install ".[dashboard]"
SNAILTRAIL_SLOW_LOG=/var/log/mysql/slow.log \
SNAILTRAIL_HISTORY_DSN=mysql://snailtrail:secret@db:3306/snailtrail \
SNAILTRAIL_TARGET_DSN=mysql://readonly:secret@db:3306/shop \
snailtrail-dashboard
```

| File | What it does |
|---|---|
| `config.py` | `Settings` (from environment variables) and `MySQLDsn` |
| `model.py` | the domain: `RunSummary`, `ClassSnapshot`, `FindingRow`, `Plan`, `Change`, `ChangePolicy`, `HistoryPoint`; turning a `Report` into rows; comparing runs |
| `ports.py` | the interfaces the service depends on: `HistoryStore`, `SchemaSource`, `Explainer` |
| `memory.py` | `MemoryHistory`: a `HistoryStore` in process memory (tests, `memory://` demo mode) |
| `mysql.py` | `Database`, `Migrator`, `MySQLHistory`, `MySQLSchemaSource` |
| `explain.py` | `MySQLExplainer` and the `EXPLAIN FORMAT=JSON` reader |
| `service.py` | `AnalysisService`: schema → analysis → plans → history, one run at a time |
| `app.py` | the FastAPI application: composition root, HTML pages, JSON API |
| `views.py` | presentation helpers: units, SQL layout, the history chart, splitting advice from SQL |
| `__main__.py` | `snailtrail-dashboard` |
| [`migrations/`](migrations) | the history schema, applied in order |
| [`templates/`](templates) | Jinja2 pages |
| [`static/`](static) | stylesheet, icon, the copy button |

## Architecture

```mermaid
classDiagram
    direction LR
    class AnalysisService {
        +run() AnalysisResult
        +busy bool
    }
    class HistoryStore {
        <<Protocol>>
        +save(report, plans) int
        +runs() list~RunSummary~
        +classes(run_id) list~ClassSnapshot~
        +history(digest) list~HistoryPoint~
        +changes(run_id, threshold)
    }
    class SchemaSource {
        <<Protocol>>
        +ddl() str
    }
    class Explainer {
        <<Protocol>>
        +explain(sql, database) Plan
    }
    HistoryStore <|.. MySQLHistory
    HistoryStore <|.. MemoryHistory
    SchemaSource <|.. MySQLSchemaSource
    Explainer <|.. MySQLExplainer
    AnalysisService --> HistoryStore
    AnalysisService --> SchemaSource
    AnalysisService --> Explainer
    AnalysisService ..> Report : snailtrail.analyze_file
    MySQLHistory --> Database
    MySQLSchemaSource --> Database
    MySQLExplainer --> Database
```

- **Ports and adapters.** `AnalysisService` depends on three `Protocol`s, not on MySQL. The
  MySQL adapters and the in-memory one are interchangeable; `build_services()` in `app.py` is
  the one place that wires them from `Settings`. The tests run the whole service and web app
  on `MemoryHistory` with fake schema sources and explainers, and a separate suite checks the
  MySQL adapters against a real server.
- **One immutable domain model.** Both stores build their rows with the same
  `snapshots_from_report()`, so they cannot drift apart; `compare()` defines what a
  regression is, and the MySQL store computes the same thing in SQL.
- **One analysis at a time.** `AnalysisService.run()` takes a non-blocking lock and raises
  `AnalysisInProgress` if a run is already going: a double-clicked button or an overlapping
  schedule does not analyse the same log twice. The analysis runs in a worker thread and
  the C++ engine releases the GIL, so the web server keeps serving meanwhile.

## What happens during a run

1. **Schema.** `MySQLSchemaSource` lists the base tables of the target database in
   `information_schema.tables` and collects `SHOW CREATE TABLE` for each; the C++ catalog
   parses that DDL. If the target is unreachable, the run continues without a schema.
2. **Analysis.** `snailtrail.analyze_file()` on the slow log, with the catalog.
3. **Plans.** For the most expensive `SELECT` classes, `MySQLExplainer` runs
   `EXPLAIN FORMAT=JSON` on the slowest sample, in its own database. Only statements the
   fingerprinter classifies as `SELECT` are explained — `EXPLAIN` does not execute a
   `SELECT`, but it would plan a `DELETE`. The JSON plan is flattened into rows (table,
   access type, key, rows per scan, filtered, filesort, temporary table).
4. **History.** Run, query classes, snapshots and findings are written in one transaction.

## The history schema

```mermaid
erDiagram
    analysis_runs ||--o{ class_snapshots : "has"
    query_classes ||--o{ class_snapshots : "measured in"
    class_snapshots ||--o{ findings : "has"
    analysis_runs {
        bigint id PK
        datetime created_at
        varchar source
        bigint events
        bigint total_query_time_us
        int critical
        int warning
        int info
    }
    query_classes {
        char digest PK
        varchar kind
        mediumtext fingerprint
        varchar label
        bigint first_run_id
        bigint last_run_id
    }
    class_snapshots {
        bigint run_id PK, FK
        char digest PK, FK
        int rank_no
        bigint calls
        double avg_time_us
        bigint p95_us
        json latency_decades
        mediumtext sample_sql
        json explain_plan
    }
    findings {
        bigint id PK
        bigint run_id FK
        char digest FK
        char rule_id
        enum severity
        text suggestion
    }
```

- `query_classes` is keyed by the 64-bit fingerprint id, stable across runs, so a query is
  followed from run to run; `class_snapshots (run_id, digest)` holds its metrics in one run,
  with a secondary index `(digest, run_id)` for its history.
- Deleting a run cascades to its snapshots and their findings.
- Upserts use the MySQL 8.0.19+ row alias (`INSERT ... AS new ON DUPLICATE KEY UPDATE
  label = new.label`) instead of the deprecated `VALUES()` function.
- **Regressions are a window function.** For every query of a run, `LAG(...) OVER w` with
  `WINDOW w AS (PARTITION BY digest ORDER BY run_id)` finds its average and call count in the
  previous run where it appeared. A `ChangePolicy` decides what is significant: a ratio of at
  least 1.5 either way, **and** an absolute difference of at least 1 ms, **and** at least 10
  calls in both runs. Without the last two conditions, a 0.1 ms statement that takes 0.3 ms
  once, or a query run five times, shows up as a "3× regression"; the lab made that obvious
  on its first run. The same policy is applied in SQL and in Python, and a test asserts both
  stores agree.
- `Migrator` applies `migrations/*.sql` in name order and records them in
  `schema_migrations`; the dashboard creates its database and migrates on start.

## Pages and API

| Route | Shows |
|---|---|
| `/` | the latest run, or how to get started |
| `/runs/{id}` | where the time went (the trail), findings, changes since the previous run, every query |
| `/runs/{id}/classes/{digest}` | a query: fingerprint, fixes, history across runs, `EXPLAIN`, latency distribution, slowest sample |
| `/runs` | every run |
| `/rules` | the advisor rules |
| `POST /analyze` | run an analysis now |
| `GET /api/health`, `/api/runs`, `/api/runs/{id}`, `/api/classes/{digest}/history`, `POST /api/analyze` | the same data as JSON; OpenAPI at `/docs` |

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `SNAILTRAIL_SLOW_LOG` | `/var/log/mysql/slow.log` | the log to analyse |
| `SNAILTRAIL_HISTORY_DSN` | `memory://` | where runs are kept: `mysql://user:password@host:port/database`, or `memory://` |
| `SNAILTRAIL_TARGET_DSN` | unset | the analysed database, for `SHOW CREATE TABLE` and `EXPLAIN` (read-only access is enough) |
| `SNAILTRAIL_DATABASE` | unset | analyse only statements run against this database |
| `SNAILTRAIL_TOP` | 50 | query classes kept per run |
| `SNAILTRAIL_THREADS` | 0 | analysis threads (0 = every core) |
| `SNAILTRAIL_EXPLAIN` / `SNAILTRAIL_EXPLAIN_TOP` | on / 15 | `EXPLAIN` the N most expensive `SELECT`s |
| `SNAILTRAIL_ANALYZE_ON_START` | on | analyse once at startup |
| `SNAILTRAIL_INTERVAL` | 0 | analyse every N seconds (0 = only on demand) |
| `SNAILTRAIL_REGRESSION_THRESHOLD` | 1.5 | ratio that counts as slower / faster |
| `SNAILTRAIL_REGRESSION_MIN_DELTA_MS` | 1 | ignore changes smaller than this, in absolute terms |
| `SNAILTRAIL_REGRESSION_MIN_CALLS` | 10 | ignore queries with fewer calls than this in either run |
| `SNAILTRAIL_HOST` / `SNAILTRAIL_PORT` | `0.0.0.0` / 8080 | where to listen |

## Design

The page is built around one element: **the trail**, a ribbon split into one segment per
query, as wide as its share of the total query time and coloured by its worst finding. It
answers the first question — where did the time go, and is it the kind I can fix? — before
anything is read. Red, amber and blue appear nowhere else: they always mean a severity.
Everything else stays quiet: one typeface (Recursive, whose monospace axis sets the SQL),
lichen-grey paper, tabular figures, SQL laid out one clause per line. Fixes that are SQL
get a copy button; prose stays prose. The ribbon draws itself once on load, left to right,
unless the reader prefers reduced motion. Light and dark themes follow the system.
