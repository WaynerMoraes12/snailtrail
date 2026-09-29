# core/src/schema

What SnailTrail knows about your tables. Everything works without it; with it, the advice
gets sharper.

| File | What it does |
|---|---|
| `catalog.cpp` | type categories, index matching, the catalog and the DDL loader |

## Where the schema comes from

From **DDL text** — the output of `mysqldump --no-data`, a folder of migrations, or
`SHOW CREATE TABLE` fetched by the Python dashboard. The catalog reuses the SQL parser
(`Parser::parse_script`) instead of a second, ad-hoc DDL reader:

```cpp
std::vector<std::string> warnings;
auto catalog = SchemaCatalog::from_ddl(read_file("schema.sql"), &warnings);
```

A `StatementVisitor` (`CatalogBuilder`) applies each statement in order:

| Statement | Effect |
|---|---|
| `CREATE TABLE` | adds (or replaces) the table with its columns and indexes |
| `ALTER TABLE ... ADD COLUMN / ADD INDEX / DROP INDEX` | modifies it |
| `CREATE [UNIQUE] INDEX ... ON t` | adds the index (the parser already turned it into an ALTER) |
| anything else | ignored (`SET`, `LOCK TABLES`, `INSERT` rows of a full dump...) |

So a migration history replays into the schema it produces. An index without a name gets
the name MySQL would give it: its first column. A statement the parser cannot read becomes
a warning and the rest of the file still loads.

## What the advisor asks it

| Question | API |
|---|---|
| Which table is this unqualified column from? | `tables_with_column("customer_id")` |
| Is `phone` a string column? (implicit-conversion check) | `find_column("phone")->category` |
| Does an index already serve `customer_id = ? AND status = ? ORDER BY created_at`? | `index_serving({"customer_id", "status"}, {"created_at"})` |
| Which existing indexes become redundant if I add that one? | `indexes_made_redundant_by(...)` |

### Index matching

A B-tree index can seek on an equality predicate for each of its leading columns, **in any
order** among the equality columns, and then on one range or sort column. So a candidate
is expressed as *(equality set, ordered tail)*:

- `Index::serves(eq, tail)`: the index's first `|eq|` columns are a permutation of `eq`,
  followed by exactly `tail`. `(customer_id, status, created_at)` serves
  `{status, customer_id} + [created_at]` and `{customer_id}`, but not `{status}` alone.
- `Index::is_prefix_of(eq, tail)`: the index is a strict leftmost prefix of the candidate,
  so the candidate makes it redundant. Only plain indexes are reported: a `PRIMARY KEY` or a
  `UNIQUE` index enforces a constraint and must stay.
- FULLTEXT and SPATIAL indexes never serve B-tree lookups.

### Type categories

`categorize_type()` maps a column type to integer, decimal, float, string, binary,
temporal, JSON or other. The advisor cares mostly about one distinction: comparing a
**string** column with a **number** makes MySQL cast every row to a number, which disables
the index on that column.

## Lookup rules

Table and column names are matched **case-insensitively** and the schema qualifier is
ignored: a slow log mentions `orders`, `Orders` and `shop.orders` for the same table. If two
schemas define the same table name, the last definition loaded wins.
