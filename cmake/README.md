# cmake/

CMake modules shared by every target.

| File | Purpose |
|---|---|
| `SnailTrailHelpers.cmake` | `snailtrail_set_warnings(<target>)` and `snailtrail_enable_sanitizers()` |
| `mingw-w64.cmake` | toolchain file cross-compiling Windows binaries with MinGW-w64 (see [`scripts/`](../scripts)) |

## `snailtrail_set_warnings(<target>)`

One strict warning set per compiler family, applied target by target so third-party code
(GoogleTest, pybind11) is never compiled with it:

- **GCC / Clang**: `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-align -Woverloaded-virtual -Wunused -Wnull-dereference -Wimplicit-fallthrough -Wformat=2`
- **MSVC**: `/W4 /permissive-` plus the off-by-default warnings for non-virtual destructors
  in polymorphic classes (C4265), always-true comparisons (C4296) and ill-formed copy
  initialisation (C4928).

`-DSNAILTRAIL_WARNINGS_AS_ERRORS=ON` turns them into errors; CI and `scripts/check.sh`
always build that way.

Every MSVC target, third-party ones included, also gets `/utf-8` (the sources are UTF-8
without a BOM, and would otherwise be read in the system code page), `/Zc:preprocessor`
(the conforming preprocessor: the traditional one mangles raw string literals passed to
GoogleTest's macros) and `/Zc:__cplusplus` (so `__cplusplus` reports C++20 instead of
`199711L`).

## `snailtrail_enable_sanitizers()`

With `-DSNAILTRAIL_SANITIZE=ON`, every target declared after the call is built and linked
with AddressSanitizer and UndefinedBehaviorSanitizer (`-fno-sanitize-recover=all`, so the
first report fails the test run). On MSVC only ASan is available.

## Build options

| Option | Default | Effect |
|---|---|---|
| `SNAILTRAIL_BUILD_CLI` | `ON` | the `snailtrail` command-line tool |
| `SNAILTRAIL_BUILD_TESTS` | `ON` | the GoogleTest suite |
| `SNAILTRAIL_BUILD_PYTHON` | `OFF` | the pybind11 extension module |
| `SNAILTRAIL_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` |
| `SNAILTRAIL_SANITIZE` | `OFF` | ASan + UBSan |
| `SNAILTRAIL_STATIC` | `OFF` | statically linked CLI |
