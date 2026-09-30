# python/tests

The pytest suite for the Python package.

| File | Covers |
|---|---|
| `conftest.py` | shared fixtures: the shop schema, a generated log, an analysed report |
| `test_native.py` | the extension: fingerprints, parse errors, rules, advice, analysis options, rendering, GIL release |
| `test_dashboard.py` | settings, view helpers, EXPLAIN parsing, the in-memory store, the service with fakes, every page and API route |
| `test_mysql.py` | the MySQL adapters against a real server: migrations, round trips, window-function regressions, `SHOW CREATE TABLE`, `EXPLAIN` (marked `mysql`) |

```bash
pip install ".[dashboard,dev]"
pytest
SNAILTRAIL_TEST_MYSQL_DSN=mysql://root:secret@127.0.0.1:3306/unused pytest -m mysql
```

The MySQL tests create and drop a throwaway database per test.

`samples/` is found relative to the repository, or through `SNAILTRAIL_SAMPLES_DIR`.
