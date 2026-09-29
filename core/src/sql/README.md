# core/src/sql

Everything SnailTrail knows about SQL text.

| File | What it does |
|---|---|
| `token.cpp` | token kind names, identifier and string-literal unquoting |
| `lexer.cpp` | the lexer |
| `keywords.cpp` | the few word lists the fingerprinter and parser need |
| `statement_kind.cpp` | SELECT / INSERT / UPDATE / DDL / ... classification |
| `fingerprint.cpp` | query fingerprints and class ids |

## Lexer

Hand-written, single pass, **zero-copy**: a `Token` is a `std::string_view` into the
statement plus its kind and byte offset.

It is **lenient by design**. A slow log is written by a server, but it can be truncated
mid-statement or carry binary data inside string literals. The lexer never throws: an
unterminated string runs to the end of the input and an unknown byte becomes a
one-character operator.

What it understands of MySQL's dialect:

| Construct | Examples |
|---|---|
| comments | `-- x` (the space is required: `1--1` is `1 - -1`), `# x`, `/* x */`, optimizer hints `/*+ BKA(t) */` |
| executable comments | `/*!40101 SET ... */` is **lexed**, because MySQL runs its content |
| strings | `'it''s'`, `"say \"hi\""`, `N'x'`, `_utf8mb4'x'` |
| identifiers | `` `order` ``, ``` `we``ird` ```, words starting with a digit (`1st_place`) |
| numbers | `42`, `3.14`, `.5`, `1e-3`, `0x1F`, `X'1F'`, `0b01`, `B'01'` |
| variables | `@x`, `@'quoted name'`, `@@session.sql_mode` |
| operators | longest match: `<=>`, `->>`, `<=`, `>=`, `<>`, `!=`, `||`, `&&`, `:=`, `->`, `<<`, `>>` |

**Keywords are not a token kind.** In MySQL `status`, `date`, `left` and `comment` are
keywords *and* legal column names, so every word is a `Word` and the parser decides from
context. The lists in `keywords.cpp` are sorted at compile time — a `static_assert` fails
the build if someone inserts a word out of order, because lookups are binary searches.

## Fingerprints

A fingerprint is a query with its values taken out. Two executions with different
values belong to the same **query class**:

```sql
SELECT * FROM `Orders` WHERE id = 42 AND status IN ('paid', 'sent')  -- dashboard
select * from orders where id = ? and status in(?+)
```

Normalisation works on the **token stream**, never with regular expressions, so a `?`, a
keyword or a comment marker inside a string literal can never confuse it:

| Rule | Before | After |
|---|---|---|
| literals become `?` | `id = 42`, `name = 'x'`, `0x1F` | `id = ?` |
| a sign is part of the number | `x = -5` | `x = ?` |
| ...but subtraction is kept | `a - 1` | `a - ?` |
| IN lists collapse | `IN (1, 2, 3, NULL)` | `in(?+)` |
| subqueries are kept | `IN (SELECT ...)` | `in (select ...)` |
| multi-row inserts collapse | `VALUES (1,'x'), (2,'y')` | `values(?+)` |
| identifiers are unquoted and lower-cased | `` `Orders` `` | `orders` |
| comments and formatting vanish | any spacing, `-- x`, `/* x */` | canonical spacing |
| trailing `;` is dropped | `select 1;` | `select ?` |

Spacing is **regenerated** from the tokens, not collapsed from the source, so two ORMs that
format the same query differently still produce one class. Function calls keep their
parenthesis attached (`count(*)`, `now()`), groups after keywords get a space
(`from (select`, `and (a or b)`).

The class id is **FNV-1a-64 of the fingerprint text** (see [`util`](../util)): the same
query class gets the same id on every platform and every run, which is what lets the
dashboard follow a query across analyses.

### Performance

The hot path of an analysis is one fingerprint per slow-log event. A `Fingerprinter`
keeps its token vector between calls and `compute_into()` reuses the caller's string, so
after warm-up fingerprinting allocates nothing. It is not thread-safe on purpose: every
worker thread owns one.

## Statement kinds

`classify_statement()` looks at the first word, skipping leading parentheses
(`(SELECT ...) UNION (SELECT ...)`). For `WITH`, it walks past the CTE definitions — which
sit inside parentheses — to the first DML word at depth 0, so
`WITH recent AS (SELECT ...) DELETE FROM ...` is a DELETE.
