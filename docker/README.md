# docker/

Container images. The [`compose.yaml`](../compose.yaml) at the root wires them into the lab.

| File | Image | Purpose |
|---|---|---|
| `Dockerfile` | `snailtrail-cli`, `snailtrail-app` | the product: CLI, dashboard, lab |
| [`mysql/`](mysql) | `snailtrail-mysql:8.4` | MySQL 8.4 with the slow log on and the shop schema loaded |
| `dev.Dockerfile` | `snailtrail-dev` | the full development toolchain |

## `Dockerfile`

One multi-stage file:

```
toolchain ──> build ──────────────> cli    (debian slim + a static snailtrail binary)
                 │
python ──> wheel ┼────────────────> app    (python slim + the snailtrail wheel + the CLI)
```

| Stage | Does |
|---|---|
| `toolchain` | Debian trixie with GCC 14, CMake, Ninja, GoogleTest |
| `build` | builds the core and the CLI with `-Werror` and **runs the C++ test suite**: an image with a failing test cannot be built |
| `cli` | the `snailtrail` binary alone, as an unprivileged user; `ENTRYPOINT ["snailtrail"]` |
| `wheel` | builds the Python wheel (scikit-build-core compiles the extension) |
| `app` | installs the wheel with the `dashboard` and `lab` extras and copies the CLI in; runs `snailtrail-dashboard` as UID 10001, with a health check on `/api/health` |

## `dev.Dockerfile`

Everything needed to build and test SnailTrail, so the host needs nothing but Docker:

- **GCC 14** and **Clang 19** (with `clang-format`, `clang-tidy`, `lld`)
- **CMake 3.31** and **Ninja**
- **GoogleTest**, so configuring never downloads anything
- **Python 3.13 headers** and **pybind11**, for the extension module
- **MinGW-w64**, which cross-compiles a native Windows `snailtrail.exe` from Linux
  (`scripts/check.sh mingw`)

```bash
docker build -f docker/dev.Dockerfile -t snailtrail-dev docker/
docker run --rm -v "$PWD:/src:ro" -v snailtrail-build:/build snailtrail-dev sh /src/scripts/check.sh clang
```

The sources are mounted **read-only**; build trees live in the `snailtrail-build` named
volume, which keeps incremental builds fast (a bind mount from a Windows or macOS host is
slow for thousands of small object files).
