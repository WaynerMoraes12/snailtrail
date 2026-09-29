# bindings/python

The pybind11 module `snailtrail._native`. The Python package in [`python/`](../../python)
re-exports it as `snailtrail`.

| File | What it does |
|---|---|
| `module.cpp` | the bindings |
| `CMakeLists.txt` | `pybind11_add_module(_native ...)`, installed into the `snailtrail` package |

It is built with `-DSNAILTRAIL_BUILD_PYTHON=ON`, which is what `pip install .` does
through scikit-build-core (see [`pyproject.toml`](../../pyproject.toml)).

## Design

- **The C++ types are the Python types.** `Finding`, `Fingerprint`, `QueryClass` and
  `Report` are the core's own structures (`advisor::Finding`, `analysis::ClassReport`,
  `analysis::Report`) exposed with read-only properties — no parallel Python model to keep
  in sync. A few values are shaped for Python on the way out: severities and statement
  kinds become strings, times stay integer microseconds, the worst sample and the
  execution flags become dicts.
- **The GIL is released during analysis.** Arguments are converted and validated while
  holding it (a bad sort key raises `ValueError` before any work starts); then
  `py::gil_scoped_release` covers the analysis itself, which touches no Python object.
- **Exceptions map to what Python code expects**: `sql::ParseError` → `snailtrail.ParseError`
  (a `ValueError` subclass carrying `.offset`), `std::system_error` → `OSError`,
  `std::invalid_argument` → `ValueError`.
- **Ownership is simple.** `SchemaCatalog` is held by a `shared_ptr`, so a catalog passed
  to several analyses stays alive as long as Python references it. Reports are returned by
  value and own everything they contain.

The type stubs live next to the Python code: [`python/snailtrail/_native.pyi`](../../python/snailtrail/_native.pyi).
