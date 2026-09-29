# core/include/snailtrail/

Public headers, one folder per module. Each folder's README lists its headers; the
matching folder under [`core/src/`](../../src) explains how the module works.

| Folder | Namespace | Headers |
|---|---|---|
| [`util/`](util) | `snailtrail::util` | strings, hash, JSON writer |
| [`sql/`](sql) | `snailtrail::sql` | tokens, lexer, fingerprints, AST, parser, SQL writer |
| [`schema/`](schema) | `snailtrail::schema` | the schema catalog |
| [`log/`](log) | `snailtrail::log` | query events, slow-log parser, mapped files, chunker, generator |
| [`stats/`](stats) | `snailtrail::stats` | histogram, summaries, query classes, aggregator |
| [`advisor/`](advisor) | `snailtrail::advisor` | findings, query facts, rules, rule engine, index advisor |
