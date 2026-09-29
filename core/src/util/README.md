# core/src/util

Small helpers every other module uses. Nothing here knows about SQL or MySQL.

| File | What it does |
|---|---|
| `strings.cpp` | case-insensitive comparison, trimming, splitting, strict number parsing, formatting for humans, civil time |
| `hash.cpp` | hexadecimal encoding of 64-bit digests |
| `json_writer.cpp` | the streaming JSON writer |

## Strings

- **ASCII-only case folding.** SQL keywords, identifiers and slow-log headers are ASCII;
  folding only `A-Z` guarantees a UTF-8 byte sequence is never altered.
- **Strict parsing.** `parse_uint("42x")` is `nullopt`, not 42: a malformed slow-log
  header must be noticed, not half-read. Numbers go through `std::from_chars` (no locale, no
  allocation); on a standard library without floating-point `from_chars` the code falls
  back to `strtod` on a bounded stack copy.
- **Durations a human reads at a glance.** `812 µs`, `45.6 ms`, `301 ms`, `1.23 s`,
  `4m 12s`, `3h 07m`: the precision shrinks as the magnitude grows, like a person would.
- **Abbreviation counts code points**, not bytes, so a Portuguese table comment or an
  emoji in a string literal never gets cut in the middle of a character.
- **Civil time without a time-zone database.** Slow logs store UTC; Howard Hinnant's
  `days_from_civil` / `civil_from_days` algorithms convert both ways in a few integer
  operations, valid for any date.

## Hash

Query-class ids are **FNV-1a, 64 bits**. The id is stored in MySQL and compared between
analyses, so it must be identical across platforms, compilers and runs — `std::hash` gives
no such guarantee. FNV-1a is a few lines, has no alignment or endianness concerns and is
`constexpr`, so its test vectors are checked at compile time. With 64 bits, the chance of a
collision among 100 000 query classes is about 3 × 10⁻¹⁰.

## JSON writer

A streaming writer: no document tree, nothing allocated per value. A small stack of open
containers decides where commas and indentation go, so callers only describe structure:

```cpp
JsonWriter w(out);
w.begin_object().field("events", 1200).key("classes").begin_array() /* ... */;
```

Misuse (a value inside an object without a key, mismatched `end_*`) throws
`std::logic_error` — a programming error, caught by the tests. Non-finite doubles become
`null` because JSON has no NaN or Infinity, and doubles are written with `std::to_chars`,
the shortest representation that round-trips.
