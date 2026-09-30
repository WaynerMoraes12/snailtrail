# templates/

Jinja2 templates of the dashboard, rendered server-side by FastAPI. No JavaScript framework
and no client-side charting: charts are SVG computed in [`../views.py`](../views.py).

| Template | Page |
|---|---|
| `base.html` | layout: header, navigation, the analyse button, error banner, footer |
| `macros.html` | severity marks |
| `logo.svg` | the snail, inlined so it takes the text colour |
| `empty.html` | before the first analysis: how to turn the slow log on |
| `run.html` | a run: the trail, findings, changes, every query |
| `class.html` | a query: fixes, history chart, `EXPLAIN`, measurements, slowest sample |
| `runs.html` | every run |
| `rules.html` | the rule catalog |

Filters available in every template: `duration` (microseconds → `45.6 ms`), `compact`,
`count`, `percent`, `when`, `data_size`, `pretty_sql`, `ratio_label`; globals:
`decade_bars()`, `fix_parts()`, `version`.
