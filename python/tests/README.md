# python/tests

The pytest suite for the Python package.

| File | Covers |
|---|---|
| `conftest.py` | shared fixtures: the shop schema, a generated log, an analysed report |
| `test_native.py` | the extension: fingerprints, parse errors, rules, advice, analysis options, rendering, GIL release |

```bash
pip install ".[dev]"
pytest
```

`samples/` is found relative to the repository, or through `SNAILTRAIL_SAMPLES_DIR`.
