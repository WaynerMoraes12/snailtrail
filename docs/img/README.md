# docs/img

Images used by the READMEs. Each one is a capture of the real thing, not a mock-up.

| File | Shows | Taken from |
|---|---|---|
| `cli-report.png` | `snailtrail analyze` in a terminal | the real MySQL 8.4 log in [`samples/`](../../samples), with the shop schema, `--top 8 --details 1` |
| `dashboard-run.png` | a run page: the trail, the findings, the changes since the previous run, the ranking | the lab, second run, after `snailtrail-lab improve` |
| `dashboard-query.png` | a query page: the fix, the measurements, the history across runs, `EXPLAIN`, the slowest execution | the lab, first run, the order-items join |

To take them again: `docker compose up -d dashboard` (the dashboard analyses the first run
on start), then `snailtrail-lab improve` and `snailtrail-lab run` in the `lab` service, press
**Analyze the log now**, and capture the pages at 1360 px wide.
