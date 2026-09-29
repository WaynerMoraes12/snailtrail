# tests/

The GoogleTest suite for `snailtrail::core`. One file per component; every test is
registered with CTest individually (`gtest_discover_tests`), so `ctest -j` runs them in
parallel and a failure names the exact case.

| File | Covers |
|---|---|
| `test_strings.cpp` | string helpers, formatting, civil time, FNV-1a reference vectors, hex |
| `test_json_writer.cpp` | structure, indentation, escaping, non-finite numbers, misuse |

## GoogleTest

`find_package(GTest)` first (the dev container ships it); otherwise CMake fetches
GoogleTest 1.15.2, pinned by SHA-256.

## Running

```bash
ctest --test-dir build --output-on-failure -j 8
ctest --test-dir build -R Fingerprint
```
