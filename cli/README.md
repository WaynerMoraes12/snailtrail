# cli/

The `snailtrail` command-line tool: a thin shell over [`core/`](../core).

```
cli/
├── CMakeLists.txt     snailtrail_cli_lib (everything but main) + the snailtrail executable
└── src/               commands, argument parsing, the entry point
```

## Commands

```
snailtrail analyze <slow.log | -> [options]   profile a slow log and advise on its worst queries
snailtrail advise  <"SQL" | ->  [options]     check one statement against the rules
snailtrail fingerprint <"SQL" | -> [--json]   print a statement's fingerprint and class id
snailtrail parse <"SQL" | -> [--sql]          print the syntax tree
snailtrail rules [--json]                     list the advisor rules
snailtrail generate [options]                 write a synthetic slow log
snailtrail help [command] | version
```

### `analyze`

| Option | Meaning |
|---|---|
| `-s, --schema FILE` | schema DDL (`mysqldump --no-data`) to verify indexes and column types |
| `-f, --format FORMAT` | `text` (default), `json` or `markdown` |
| `-o, --output FILE` | write the report to a file |
| `-t, --threads N` | worker threads (default: every core) |
| `-n, --top N` | keep the N most expensive query classes (default 20, 0 = all) |
| `--details N` | classes detailed in text and Markdown (default 10) |
| `--sort KEY` | `time`, `calls`, `avg`, `p95`, `max` or `rows` |
| `-d, --database NAME` | only statements run against this database |
| `--min-severity LEVEL` | hide findings below `info`, `warning` or `critical` |
| `--disable RULES` | comma-separated rule ids or names |
| `--fail-on LEVEL` | exit with status 2 when a finding reaches this severity |
| `--no-advice` | profile only |
| `-w, --width N`, `--color`, `--no-color` | presentation |

`-` reads the log from standard input, in constant memory:

```bash
ssh db1 'cat /var/log/mysql/slow.log' | snailtrail analyze - --schema schema.sql
```

### As a CI gate

```bash
snailtrail analyze slow.log --schema schema.sql --fail-on critical --format markdown >> "$GITHUB_STEP_SUMMARY"
snailtrail advise "$(cat new_query.sql)" --schema schema.sql --fail-on warning
```

### Exit status

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | runtime error (unreadable file, unknown format...) |
| 2 | a finding reached `--fail-on` |
| 64 | usage error (the BSD `EX_USAGE` convention) |

## Design

The **Command** pattern: each subcommand is a class implementing `Command` — `name()`,
`summary()`, `usage()`, a declarative list of `OptionSpec`s, and `run(args, console)`.
`App` registers them, dispatches on the first argument, prints help generated from the
specs, and turns `UsageError` into status 64 and other exceptions into status 1.

`run()` never touches `std::cout` or `std::cin`: it gets a `Console` holding the three
streams, whether colour is wanted and the terminal width. `main()` wires the real streams
(and, on Windows, switches the console to UTF-8 and enables ANSI escapes); the tests wire
string streams and drive the whole CLI in-process — see
[`tests/test_cli.cpp`](../tests/test_cli.cpp).

Colours default to *on* when standard output is a terminal and `NO_COLOR` is unset; the
width comes from `COLUMNS`, else 100.
