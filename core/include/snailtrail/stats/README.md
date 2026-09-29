# snailtrail/stats

Public headers of the `snailtrail::stats` module. Implementation notes:
[`core/src/stats`](../../../src/stats).

| Header | Declares |
|---|---|
| `histogram.hpp` | `LatencyHistogram`: log-linear buckets, percentiles, per-decade counts, merge |
| `summary.hpp` | `Summary<T>`: count, sum, min, max, mean |
| `query_class.hpp` | `QueryClass` (every metric of one fingerprint), `Sample`, `Tally` |
| `aggregator.hpp` | `Aggregator` (events → query classes), `Totals` |
