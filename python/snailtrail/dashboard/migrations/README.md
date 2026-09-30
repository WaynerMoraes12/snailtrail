# migrations/

The MySQL schema of the dashboard's history, as ordered SQL files. `Migrator` (in
[`../mysql.py`](../mysql.py)) applies every `*.sql` file not yet recorded in
`schema_migrations`, in name order, when the dashboard starts.

| File | Creates |
|---|---|
| `001_history.sql` | `analysis_runs`, `query_classes`, `class_snapshots`, `findings` |

A migration is split on `;` and each statement runs in turn, so a new migration is a new
file with a higher number; applied files are never edited. The schema itself is described
in the [dashboard README](../README.md#the-history-schema).
