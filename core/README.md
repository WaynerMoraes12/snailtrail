# core/

The SnailTrail library (`snailtrail::core`): a static C++20 library with no runtime
dependency beyond the standard library and the platform's threads. The CLI, the tests and
the Python extension all link it.

```
core/
├── CMakeLists.txt          the target and its source list
├── include/snailtrail/     public headers, one folder per module
└── src/                    implementation, same layout
```

## Modules

| Module | Responsibility |
|---|---|
| [`util`](src/util) | strings, human-readable formatting, civil time, FNV-1a, a streaming JSON writer |

## Conventions

- **Namespaces follow folders**: `snailtrail::util`, `snailtrail::sql`, ...
- **Naming**: `PascalCase` types, `snake_case` functions and variables, `trailing_` members.
- **Views, not copies**: parsing works on `std::string_view` into the caller's buffer; a
  value is copied only when it has to outlive that buffer.
- **No exceptions on the hot path**: the lexer and the fingerprinter are `noexcept` where
  it matters and lenient with malformed input, because a slow log can be truncated.
- **Built with `-Werror`** under GCC and Clang (see [`cmake/`](../cmake)).
