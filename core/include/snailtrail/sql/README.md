# snailtrail/sql

Public headers of the `snailtrail::sql` module. Implementation notes:
[`core/src/sql`](../../../src/sql).

| Header | Declares |
|---|---|
| `token.hpp` | `TokenKind`, `Token` (a view into the statement), `unquote_identifier()`, `unquote_string()` |
| `lexer.hpp` | `Lexer`: `next()`, `tokenize()`, `tokenize_into()` |
| `keywords.hpp` | `is_reserved_word()`, `is_operator_keyword()`, `is_aggregate_function()` |
| `statement_kind.hpp` | `StatementKind`, `classify_statement()`, `is_dml()` |
| `fingerprint.hpp` | `Fingerprint`, `Fingerprinter`, `fingerprint()` |
