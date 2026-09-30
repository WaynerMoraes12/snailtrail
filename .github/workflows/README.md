# .github/workflows

| Workflow | Runs |
|---|---|
| `ci.yml` | on every push to `main` and every pull request |

## `ci.yml`

| Job | Checks |
|---|---|
| Linux (GCC 14, Clang 18) | builds with `-Werror`, runs the C++ suite; the GCC job also analyses a 200 000-event generated log and publishes the Markdown report as the job summary |
| Linux (ASan + UBSan) | the C++ suite under AddressSanitizer and UndefinedBehaviorSanitizer |
| Windows (MSVC) | builds with `/W4 /WX`, runs the C++ suite |
| macOS (Apple Clang) | builds and runs the C++ suite |
| Python and MySQL | builds the wheel, `ruff`, then pytest — including the MySQL integration tests against a `mysql:8.4` service container |
| Docker lab end to end | builds every image (whose build runs the C++ suite), starts MySQL and the dashboard, seeds and runs the lab workload, analyses through the dashboard API and asserts on the findings and `EXPLAIN` plans, then runs the CLI image on the same log |
