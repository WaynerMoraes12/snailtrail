# core/src/stats

From a stream of events to a table of query classes.

| File | What it does |
|---|---|
| `histogram.cpp` | the latency histogram |
| `query_class.cpp` | per-class accumulation and merging |
| `aggregator.cpp` | fingerprinting events into classes, merging aggregators |

## Latency histogram

Percentiles need the distribution, and a class can have millions of executions: storing
them is out. `LatencyHistogram` is a **log-linear** histogram in the spirit of
HdrHistogram, over integer microseconds:

- values below 64 µs get one bucket each (exact);
- above, every power of two is split into **32 sub-buckets**, so a bucket is never wider
  than 1/32 of its lower bound. A percentile is reported as the bucket's midpoint, so it is
  within **±1.6 %** of the true value — and then clamped to the class's exact min and max.

```
index(v) = v                                   for v < 64
index(v) = (shift + 1) · 32 + (v >> shift) − 32   where shift = ⌊log₂ v⌋ − 5
```

The buckets tile the line with no gap or overlap (`upper(i) == lower(i + 1)`, checked by
the tests), the index needs one `std::bit_width` and a shift — no floating point, no `log`
— and two histograms merge by adding counts. The vector only grows to the highest bucket
used: a class whose worst execution took 10 s holds about 640 counters.

`decades()` folds the buckets into eight columns — 1 µs, 10 µs, … 1 s, 10 s+ — for the
latency sparkline in the text report.

## Query classes

`QueryClass` accumulates everything the reports and the advisor need for one fingerprint:

| Metric | Kept as |
|---|---|
| query time, lock time | `Summary` + histogram (query time) |
| rows sent / examined / affected | `Summary` |
| bytes sent | sum |
| full scan, filesort, tmp table, tmp table on disk | a count per flag |
| first / last seen | min / max timestamp |
| databases, users | small tallies, sorted on read |
| the **worst sample** | the slowest execution: its SQL, database, user, time, rows |

The worst sample is the statement the advisor parses and a human reads: the real text,
with its real values, of the execution that hurt most.

## Aggregator and determinism

`Aggregator::add()` is the hot path: one fingerprint (into a reused buffer), one hash-map
lookup by the 64-bit id, one `QueryClass::add()`. Each worker thread owns an aggregator;
`merge()` folds them together **in chunk order**.

The result does not depend on the number of threads — not approximately, **bit for bit**:

- all sums are integers (microseconds, rows), so addition is associative;
- histograms merge by exact addition;
- the worst sample is replaced only by a *strictly* slower one, so on a tie the earliest
  execution in the file wins, both within a chunk and across merges in chunk order;
- tallies are sorted by (count, name) when read, never exposed in insertion order;
- `take_classes()` returns classes sorted by id, not in hash-map order.

`ChunkedAggregationIsBitIdenticalToSequential` checks every metric of every class after
splitting 8 000 generated events into 16 chunks.
