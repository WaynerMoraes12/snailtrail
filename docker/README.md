# docker/

Container images.

| File | Image | Purpose |
|---|---|---|
| `dev.Dockerfile` | `snailtrail-dev` | the full development toolchain |

## `dev.Dockerfile`

Everything needed to build and test SnailTrail, so the host needs nothing but Docker:

- **GCC 14** and **Clang 19** (with `clang-format`, `clang-tidy`, `lld`)
- **CMake 3.31** and **Ninja**
- **GoogleTest**, so configuring never downloads anything
- **Python 3.13 headers** and **pybind11**, for the extension module
- **MinGW-w64**, which cross-compiles a native Windows `snailtrail.exe` from Linux

```bash
docker build -f docker/dev.Dockerfile -t snailtrail-dev docker/
docker run --rm -v "$PWD:/src:ro" -v snailtrail-build:/build snailtrail-dev sh /src/scripts/check.sh clang
```

The sources are mounted **read-only**; build trees live in the `snailtrail-build` named
volume, which keeps incremental builds fast (a bind mount from a Windows or macOS host is
slow for thousands of small object files).
