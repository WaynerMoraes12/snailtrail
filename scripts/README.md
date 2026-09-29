# scripts/

Helpers for building and checking the project.

| Script | Runs | Purpose |
|---|---|---|
| `check.sh` | inside `snailtrail-dev` | configure, build (with `-Werror`) and run the test suite |

## `check.sh [gcc|clang|asan] [Debug|Release]`

| Argument | Compiler | Notes |
|---|---|---|
| `gcc` (default) | GCC 14 | |
| `clang` | Clang 19 | |
| `asan` | Clang 19 | AddressSanitizer + UndefinedBehaviorSanitizer |

Each combination gets its own build tree (`/build/<compiler>-<type>`), so switching
between them never forces a full rebuild.

```bash
docker run --rm -v "$PWD:/src:ro" -v snailtrail-build:/build snailtrail-dev sh /src/scripts/check.sh asan
```
