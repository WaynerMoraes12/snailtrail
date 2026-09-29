# cli/src

| File | What it does |
|---|---|
| `arguments.hpp/.cpp` | `OptionSpec`, `Arguments` (long, short, `--key=value`, `--`), `UsageError` |
| `command.hpp` | `Command`, `App`, `Console`, `ExitCode` and the command factories |
| `app.cpp` | registration, dispatch, generated help, error-to-exit-code mapping |
| `cmd_analyze.cpp` | `analyze` |
| `cmd_advise.cpp` | `advise` |
| `cmd_inspect.cpp` | `fingerprint` and `parse` (with a caret under the parse error) |
| `cmd_rules.cpp` | `rules` |
| `cmd_generate.cpp` | `generate` |
| `main.cpp` | the entry point: console setup, terminal detection, `App().run()` |

Every file except `main.cpp` goes into `snailtrail_cli_lib`, which the test suite links.
