# samples/

Example inputs, used by the test suite, the documentation and the demo commands.

| File | What it is |
|---|---|
| `shop_schema.sql` | the schema of the lab's e-commerce database (`customers`, `products`, `orders`, `order_items`, `reviews`, `sessions`), in `mysqldump --no-data` format |
| `mysql-8.4-slow.log` | a real slow log, not a generated one: 1,918 statements written by MySQL 8.4.11 with `log_slow_extra=ON` while the lab workload ran against the `shop` database, before `improve` added any index |

## The `shop` database

A small online store, designed so that the lab workload hits every kind of problem
SnailTrail looks for:

| Table | Deliberate gap |
|---|---|
| `orders` | only `idx_orders_customer (customer_id)`: filtering by status or sorting by date forces a filesort |
| `order_items` | no index on `order_id`: every join from `orders` scans the table |
| `customers` | `phone` is a `varchar`, so `WHERE phone = 5511...` disables its index |
| `sessions` | no index on `expires_at`: the cleanup job scans everything |
| `products` | `name LIKE '%...%'` searches, `ORDER BY RAND()` for "featured" items |

## The real log

`mysql-8.4-slow.log` is the first part of the log the [lab](../python/snailtrail/lab) produces,
cut at an event boundary. It keeps everything a live server writes and a generator would not:
the start-up banner (`/usr/sbin/mysqld, Version: ...` and the `Tcp port` / `Time Id Command`
lines), `use shop;` only where a connection changed database, `SET timestamp=` before each
statement, `# administrator command: Ping;` and `Quit;` entries, the driver's `SET NAMES`, a
`COMMIT` after every write, and every `log_slow_extra` attribute (`Thread_id`, `Errno`,
`Bytes_sent`, `Start` / `End`, the handler and sort counters).

The C++ and Python suites analyse it and expect what the lab shows: reading an order's items
(`order_items` joined to `products`, by `order_id`) takes more than half of the query time with
a full scan of `order_items`, and SnailTrail's first
suggestion is `ADD INDEX idx_order_items_order_id (order_id)`.

```bash
snailtrail analyze samples/mysql-8.4-slow.log --schema samples/shop_schema.sql --database shop
```
