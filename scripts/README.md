# scripts/

Helpers for building and checking the project.

| Script | Runs | Purpose |
|---|---|---|
| `check.sh` | inside `snailtrail-dev` | configure, build (with `-Werror`) and run the test suite |

## `check.sh [gcc|clang|asan|mingw] [Debug|Release]`

| Argument | Compiler | Notes |
|---|---|---|
| `gcc` (default) | GCC 14 | |
| `clang` | Clang 19 | |
| `asan` | GCC 14 | AddressSanitizer + UndefinedBehaviorSanitizer |
| `mingw` | MinGW-w64 (GCC 14) | cross-compiles static Windows binaries: `snailtrail.exe` and `snailtrail_tests.exe` |

Each combination gets its own build tree (`/build/<compiler>-<type>`), so switching
between them never forces a full rebuild.

```bash
docker run --rm -v "$PWD:/src:ro" -v snailtrail-build:/build snailtrail-dev sh /src/scripts/check.sh asan
```

### Windows binaries from Linux

The `mingw` mode copies both executables to `/out` when that folder is mounted:

```bash
docker run --rm -v "$PWD:/src:ro" -v "$PWD/build/windows:/out" -v snailtrail-build:/build \
    snailtrail-dev sh /src/scripts/check.sh mingw Release
SNAILTRAIL_SAMPLES_DIR="$PWD/samples" build/windows/snailtrail_tests.exe
```

They are fully static, so they run on any 64-bit Windows 10/11 without a runtime. The
test suite is the same one CI runs on Linux; running it natively exercises the Windows
code paths (`CreateFileMapping`, the UTF-8 console).
