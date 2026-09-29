# core/src/log

Turning a slow query log — possibly gigabytes of it — into a stream of `QueryEvent`s.

| File | What it does |
|---|---|
| `slow_log_parser.cpp` | the line-driven parser for MySQL, Percona Server and MariaDB slow logs |
| `mapped_file.cpp` | memory-maps the log file (`mmap` / `CreateFileMapping`) |
| `chunker.cpp` | splits a mapped log at event boundaries for parallel parsing |
| `generator.cpp` | writes realistic synthetic slow logs (tests, benchmarks, demos) |

## The format

A slow-log entry is a few `#` header lines, optional context lines, then the statement:

```
# Time: 2024-01-15T10:23:45.123456Z
# User@Host: app[app] @ app-1 [10.0.0.12]  Id:    42
# Query_time: 0.045123  Lock_time: 0.000004 Rows_sent: 20  Rows_examined: 3120 Thread_id: 42 ... Sort_scan_count: 1 ...
use shop;
SET timestamp=1705314225;
SELECT id, total FROM orders WHERE customer_id = 812 AND status = 'paid' ORDER BY created_at DESC LIMIT 20;
```

Every server writes it a little differently, and the parser reads all of them:

| Server | Differences handled |
|---|---|
| MySQL 5.7 / 8.x | ISO `# Time:`; with `log_slow_extra=ON` (8.0.14+), ~20 extra counters on the `Query_time` line |
| Percona Server | `# Schema:` line, `Rows_affected`, `Bytes_sent`, `Full_scan: Yes`, `Filesort: Yes`, indented InnoDB lines |
| MariaDB | `# Time: 240115 10:23:45`, written only when the second changes; `# Thread_id: ... Schema: ...` |

`use db;` is written only when the database **changes**, so the current database is state
that carries from one event to the next; a `Schema:` header, when present, wins.
`# administrator command: Quit;` entries are counted as skipped, and the banner a server
writes when it (re)starts is recognised even in the middle of the file.

## Parser design

A three-state machine — `Idle`, `Header`, `Body` — fed **one line at a time**:

- `# Time:`, `# User@Host:` and `# Query_time:` start an event (whichever comes first,
  because MariaDB omits `# Time:` and some tools strip headers); any other `#` line in the
  header is a list of `Key: value` attributes.
- In the body, `#` lines are part of the statement (a multi-line query can contain
  comments); only an event-starting header ends the body.
- `use db;` and `SET timestamp=N;` right after the header are context, not SQL.

Feeding lines keeps the parser independent of where they come from: a memory-mapped file,
`stdin`, a Python string. The event it hands to the callback holds **views into the
parser's own reusable buffers**, valid for the duration of the callback: parsing a line
allocates nothing once the buffers have grown.

### Exact numbers

`Query_time` and `Lock_time` are parsed straight into **integer microseconds**
(`0.045123` → `45123`), never through a `double`. Sums over millions of events are then
exact and associative, which is what makes a parallel analysis produce *bit-identical*
results for any number of threads.

### Execution flags

The advisor wants to know *how* a statement ran, not just how long it took. The flags are
read from whichever counters the server provides:

| Flag | Percona / MariaDB | MySQL 8 `log_slow_extra` |
|---|---|---|
| `FullScan` | `Full_scan: Yes` | `Read_rnd_next > 0` |
| `Filesort` | `Filesort: Yes` | `Sort_scan_count + Sort_range_count > 0` |
| `TmpTable` | `Tmp_table: Yes` | `Created_tmp_tables > 0` |
| `TmpTableOnDisk` | `Tmp_table_on_disk: Yes` | `Created_tmp_disk_tables > 0` |

## Memory mapping

`MappedFile` maps the whole file read-only (`MAP_PRIVATE` + `MADV_SEQUENTIAL` on POSIX,
`CreateFileMapping` + `FILE_FLAG_SEQUENTIAL_SCAN` on Windows) and exposes it as one
`std::string_view`. The kernel pages it in as the parser advances; nothing is copied into
user space. It is RAII and move-only; an empty file is a valid, empty view; failures throw
`std::system_error` with the OS error.

## Parallel parsing: the chunker

A log is sequential, but events are independent — except for one piece of state, the
current database. `split_log(log, parts)`:

1. cuts the log at `size / parts` intervals, moving each cut forward to the next event
   start (`# User@Host:`, backed up to its `# Time:` line if it has one);
2. finds, for every chunk, the last `use db;` it contains (a backwards search), so each
   chunk knows the database in effect **before** its first line: the last `use` of the
   closest preceding chunk that has one.

Each worker then parses its chunk with its own `SlowLogParser` seeded with that database,
and gets exactly the events a sequential pass would have produced — the test suite checks
that on 6 000 generated events split 16 ways. Logs smaller than `min_chunk_bytes` per part
are not split: below that, starting threads costs more than it saves.

## Generator

`SlowLogGenerator` writes a MySQL 8.4 slow log (with `log_slow_extra`) of any length from
20 scenarios on the lab's `shop` schema: the cheap primary-key lookups that dominate the
count, and the expensive patterns the advisor exists to catch — a join on an unindexed
column, `LOWER(email) = ...`, `LIKE '%term%'`, deep `LIMIT` offsets, `ORDER BY RAND()`,
`NOT IN (SELECT ...)`, a forgotten join condition, an `UPDATE` without `WHERE`. Latencies are
log-normal around a per-scenario median; the same SQL is sometimes written with and
without backticks, as ORMs do.

It is **deterministic for a seed**: `std::mt19937_64` is fully specified by the standard,
and the distributions are implemented here (Box–Muller for the normal) because
`std::normal_distribution` and friends may produce different sequences on different
standard libraries.
