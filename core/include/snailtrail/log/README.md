# snailtrail/log

Public headers of the `snailtrail::log` module. Implementation notes:
[`core/src/log`](../../../src/log).

| Header | Declares |
|---|---|
| `query_event.hpp` | `QueryEvent` (one slow-log entry, as views), `ExecutionFlag` |
| `slow_log_parser.hpp` | `SlowLogParser`, `parse_microseconds()`, `parse_log_time()`, `for_each_line()` |
| `mapped_file.hpp` | `MappedFile`, a read-only RAII memory map (POSIX and Windows) |
| `chunker.hpp` | `LogChunk`, `split_log()`, `find_event_start()`, `last_use_database()` |
| `generator.hpp` | `SlowLogGenerator`, `GeneratorOptions`, `Random` |
