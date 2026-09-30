# docker/mysql

The lab's MySQL: the official `mysql:8.4` image, configured to log every statement.

| File | What it does |
|---|---|
| `Dockerfile` | adds the files below and a log directory the other containers can read |
| `snailtrail.cnf` | turns the slow log on |
| `00-snailtrail.sh` | first start only: creates the `snailtrail` database and user |

## `snailtrail.cnf`

| Setting | Why |
|---|---|
| `slow_query_log = ON`, `slow_query_log_file = /var/log/mysql-slow/slow.log` | the log SnailTrail reads, on a volume shared with the dashboard |
| `long_query_time = 0` | log **every** statement: slowness is a distribution, and a 2 ms query run 50 000 times costs more than one 10 s report |
| `log_slow_extra = ON` | MySQL 8.0.14+ adds ~20 counters per statement; SnailTrail reads the full-scan, filesort and temporary-table evidence from them |
| `log_timestamps = UTC` | unambiguous `# Time:` lines |

The container runs with `UMASK=0644`, so the log file it creates is readable by the
dashboard's unprivileged user.

## First start

The official entrypoint runs `/docker-entrypoint-initdb.d` in order on an empty data
directory:

1. `00-snailtrail.sh` creates the `snailtrail` database (the dashboard's history) and a
   `snailtrail` user with full rights on it and **read-only** rights on `shop`: enough for
   `SHOW CREATE TABLE` and `EXPLAIN`, nothing more.
2. `10-shop-schema.sql` — [`samples/shop_schema.sql`](../../samples/shop_schema.sql),
   copied in at build time — creates the shop's tables in `shop`.
