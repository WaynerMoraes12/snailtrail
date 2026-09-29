# snailtrail/util

Public headers of the `snailtrail::util` module. Implementation notes:
[`core/src/util`](../../../src/util).

| Header | Declares |
|---|---|
| `strings.hpp` | ASCII case helpers, trim/split/join, strict number parsing, human-readable durations, counts and sizes, UTF-8-aware abbreviation, civil time conversion |
| `hash.hpp` | `Fnv1a64` (incremental, `constexpr`), `fnv1a_64()`, `to_hex()` / `from_hex()` |
| `json_writer.hpp` | `JsonWriter`, a streaming JSON writer |
