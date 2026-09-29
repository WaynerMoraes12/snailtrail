# tests/

The GoogleTest suite for `snailtrail::core`. One file per component; every test is
registered with CTest individually (`gtest_discover_tests`), so `ctest -j` runs them in
parallel and a failure names the exact case.

| File | Covers |
|---|---|
| `test_strings.cpp` | string helpers, formatting, civil time, FNV-1a reference vectors, hex |
| `test_json_writer.cpp` | structure, indentation, escaping, non-finite numbers, misuse |
| `test_lexer.cpp` | every token kind, comments, executable comments, garbage input |
| `test_fingerprint.cpp` | normalisation rules, IN and VALUES collapsing, statement kinds |
| `test_parser.cpp` | every statement and clause, precedence, mysqldump DDL, scripts, error positions, pathological nesting |
| `test_sql_writer.cpp` | SQL round trips, minimal parenthesisation, identifier quoting, the tree printer |
| `test_schema.cpp` | type categories, loading a mysqldump file, ALTER/CREATE INDEX replay, index matching |
| `test_slow_log_parser.cpp` | MySQL 8 (with `log_slow_extra`), Percona and MariaDB formats, time formats, restarts, CRLF |
| `test_chunker.cpp` | event boundaries, chunked parsing equals sequential parsing, memory mapping, the generator |
| `test_stats.cpp` | histogram tiling and accuracy, summaries, query classes, bit-identical chunked aggregation |
| `test_advisor.cpp` | fact collection, every rule's positive and negative cases, the index advisor against a real schema, the engine |

## GoogleTest

`find_package(GTest)` first (the dev container ships it); otherwise CMake fetches
GoogleTest 1.15.2, pinned by SHA-256.

## Running

```bash
ctest --test-dir build --output-on-failure -j 8
ctest --test-dir build -R Fingerprint
```
