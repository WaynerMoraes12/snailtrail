# snailtrail

The importable package. The engine itself is compiled C++ — the `_native` extension, built
from [`bindings/python`](../../bindings/python) — and this folder is what surrounds it.

| File | What it does |
|---|---|
| `__init__.py` | the public API: re-exports the extension's functions and types, so user code imports `snailtrail`, never `snailtrail._native` |
| `_native.pyi` | type stubs for the extension: signatures, keyword-only options, return types, for editors and type checkers |
| `py.typed` | the PEP 561 marker that tells type checkers the package ships its own types |
| [`dashboard/`](dashboard) | the web dashboard, with its history in MySQL |
| [`lab/`](lab) | the shop database, its workload and `improve` |

`_native` is not in the repository: `pip install .` compiles it and places it here, next
to `__init__.py`.

## The public API

| Name | What it is |
|---|---|
| `analyze_file`, `analyze_text` | analyse a slow log; return a `Report` |
| `advise` | check one statement; return a list of `Finding` |
| `fingerprint` | normalise a statement; return a `Fingerprint` (`id`, `kind`, `text`) |
| `parse_tree` | the syntax tree as text; raises `ParseError` |
| `rules` | the advisor's rule catalog |
| `generate_log` | a synthetic MySQL slow log |
| `SchemaCatalog` | tables and indexes from `CREATE TABLE` statements |
| `Report`, `QueryClass`, `Finding` | the results, read-only |
| `__version__` | the engine's version, from the C++ build |

Usage and details are in the [package README](../README.md).
