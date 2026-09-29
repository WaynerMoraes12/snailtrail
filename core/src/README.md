# core/src/

Implementation of the library, one folder per module, mirroring
[`core/include/snailtrail/`](../include/snailtrail). Each folder's README explains the
design of its module: what problem it solves, the decisions behind it and the trade-offs.

| Folder | Module |
|---|---|
| [`util/`](util) | shared helpers |
| [`sql/`](sql) | lexer, fingerprints, AST and parser |
| [`schema/`](schema) | schema catalog |
| [`log/`](log) | slow-log parsing, memory mapping, chunking, generator |
