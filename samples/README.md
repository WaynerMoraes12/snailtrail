# samples/

Example inputs, used by the test suite, the documentation and the demo commands.

| File | What it is |
|---|---|
| `shop_schema.sql` | the schema of the lab's e-commerce database (`customers`, `products`, `orders`, `order_items`, `reviews`, `sessions`), in `mysqldump --no-data` format |

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
