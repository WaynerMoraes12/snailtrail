# snailtrail.lab

A realistic workload for trying SnailTrail end to end: a small online shop on MySQL 8.4
with the mistakes real applications make, and a command to fix them.

```bash
snailtrail-lab all                  # wait for MySQL, seed the shop, replay the workload
snailtrail-lab seed --scale medium  # tiny | small (default) | medium
snailtrail-lab run --iterations 5000 --concurrency 8
snailtrail-lab run --seed 421337           # replay an earlier run exactly
snailtrail-lab improve --dry-run    # print the indexes SnailTrail suggests
snailtrail-lab improve              # create them, then start a new slow log
snailtrail-lab rotate               # archive the slow log and start a new one
```

`--dsn` (or `SNAILTRAIL_LAB_DSN`) points at the database; `--slow-log`
(`SNAILTRAIL_LAB_SLOW_LOG`) at the slow log, which `improve` and `rotate` need to reach.

| File | What it does |
|---|---|
| `seed.py` | `Seeder` and the `SCALES`: fills the six tables of [`samples/shop_schema.sql`](../../../samples/shop_schema.sql) |
| `workload.py` | the 20 `SCENARIOS` and the concurrent `Workload` runner |
| `improve.py` | `Improver`: reads the schema, analyses the log, applies the ST001 indexes, rotates the log |
| `__main__.py` | `snailtrail-lab` |

## Seeding inside the server

Rows are generated **by MySQL itself**, one `INSERT ... SELECT` per table over a recursive
CTE (`WITH RECURSIVE seq (n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 240000)`),
with every column derived from `n` — `CRC32(n)` for spread-out choices, `SHA2(CONCAT('session-',
n), 256)` for session tokens. Nothing crosses the network row by row: the small scale (about
400 000 rows) seeds in seconds, and the same `n` always produces the same row, so the
workload can compute valid ids, e-mails, phone numbers and tokens without querying.

The seeding session sets `long_query_time = 3600`, so its statements stay out of the slow
log: the log records only the workload.

## The workload

Twenty scenarios, weighted like a real application — mostly cheap primary-key lookups,
with the expensive patterns mixed in. Each worker thread holds its own connection and its
own seeded random generator.

Every run draws a **new seed** and logs it (`workload seed 421337`); `--seed` replays that
run statement for statement. A fixed default would make a second run repeat the first one's
writes exactly — `UPDATE orders SET status = 'cancelled', ... WHERE id = 66723` again, on a
row that already holds those values — and InnoDB skips a write that changes nothing: no
redo, no binary log, no fsync. The lab runs in autocommit on MySQL's durable defaults
(`innodb_flush_log_at_trx_commit = 1`, `sync_binlog = 1`), where that fsync is most of a
write's cost, so those no-op updates came out 30× faster after `improve` for reasons that
had nothing to do with indexes. Fresh seeds keep a before-and-after comparison honest.

| Scenario | SQL | What SnailTrail should find |
|---|---|---|
| product page | `SELECT * FROM products WHERE id = ?` (with and without backticks) | `SELECT *` only; both spellings are one class |
| session check | lookup by unique token | nothing |
| customer orders | `WHERE customer_id = ? AND status = ? ORDER BY created_at DESC LIMIT 20` | index `(customer_id, status, created_at)`, drop `idx_orders_customer` |
| order items | join on `order_items.order_id`, which has no index | index on `order_id`; a full scan per call |
| login | `WHERE LOWER(email) = ?` | non-sargable; the unique index on `email` already works without `LOWER()` |
| search | `name LIKE '%term%'` | leading wildcard → FULLTEXT |
| reviews page | `LIMIT 8000, 20` on some calls | deep pagination → keyset |
| phone lookup | `WHERE phone = 5511900000123` on a `varchar` | implicit conversion → quote it, index it |
| featured | `ORDER BY RAND() LIMIT 8` | random sort of the whole table |
| email or phone | `email = ? OR phone = ?` | OR across columns, `phone` unindexed |
| stock check | `IN (...)` with 250–400 ids | above `eq_range_index_dive_limit` |
| daily revenue | `WHERE DATE(created_at) >= ? GROUP BY DATE(created_at)` | non-sargable, range rewrite |
| categories | `HAVING category <> 'books'` | belongs in `WHERE` |
| purge sessions | `DELETE ... WHERE expires_at < ?` | index on `expires_at` |
| inactive customers | `NOT IN (SELECT customer_id FROM orders)` | NULL trap → `NOT EXISTS` |
| coupon report | `FROM customers c, orders o` with no join condition | cartesian product, `o.customer_id = c.id` |
| reset tiers | `UPDATE customers SET tier = 1` | no `WHERE` |
| place order, add items, update status | writes by primary key | nothing |

The test suite runs every scenario through the advisor and checks that each one triggers
the rule it was written for.

## Closing the loop

`improve` asks SnailTrail for its ST001 suggestions against the live schema, applies each
`ALTER TABLE ... ADD INDEX`, and rotates the slow log (rename + `FLUSH SLOW LOGS`). Run the
workload again and the dashboard's next analysis shows the same queries, much faster, under
*Changes since the previous run* — on the small scale, the order-items join goes from 60 ms
to 0.3 ms and the session purge from 13 ms to 0.2 ms. It only ever adds indexes, and only
to the lab database.
