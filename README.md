# SnailTrail 🐌

**Every slow query leaves a trail.** SnailTrail reads MySQL's slow query log, groups every
statement into query classes, ranks them by the time they cost the server and tells you
how to fix them — down to the `ALTER TABLE ... ADD INDEX` that is missing.

Modern C++20 core, Python on top, MySQL as both the target and the history store, Docker
for everything.

## Build and test

No compiler on the host is needed: the toolchain lives in a container.

```bash
docker build -f docker/dev.Dockerfile -t snailtrail-dev docker/
docker run --rm -v "$PWD:/src:ro" -v snailtrail-build:/build snailtrail-dev sh /src/scripts/check.sh gcc
```

With a local toolchain (GCC 13+, Clang 17+ or MSVC 19.38+, CMake 3.21+):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Repository map

| Folder | What lives there |
|---|---|
| [`core/`](core) | the C++20 library: parsing, statistics, the advisor, reports |
| [`cli/`](cli) | the `snailtrail` command-line tool |
| [`bindings/`](bindings) | the pybind11 extension module |
| [`python/`](python) | the `snailtrail` Python package |
| [`tests/`](tests) | the GoogleTest suite |
| [`samples/`](samples) | example schema and slow logs |
| [`cmake/`](cmake) | shared compiler settings |
| [`docker/`](docker) | container images |
| [`scripts/`](scripts) | build and check helpers |

Every folder has its own README explaining what it holds and why.

## License

[MIT](LICENSE) © Wayner Moraes
