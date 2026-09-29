#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "snailtrail/log/chunker.hpp"
#include "snailtrail/log/generator.hpp"
#include "snailtrail/log/slow_log_parser.hpp"
#include "snailtrail/stats/aggregator.hpp"

using namespace snailtrail;
using stats::LatencyHistogram;

namespace {

log::QueryEvent event(std::string_view sql, std::uint64_t us, std::string_view db = "shop",
                      std::int64_t ts = 1705312800) {
    log::QueryEvent e;
    e.sql = sql;
    e.database = db;
    e.user = "app";
    e.query_time_us = us;
    e.rows_examined = us * 10;
    e.rows_sent = 1;
    e.timestamp = ts;
    return e;
}

stats::Aggregator aggregate(std::string_view text, std::string_view db = {}) {
    stats::Aggregator agg;
    log::SlowLogParser::parse_text(text, [&](const log::QueryEvent& e) { agg.add(e); }, db);
    return agg;
}

}

TEST(Histogram, BucketsTileTheLineWithBoundedWidth) {
    std::size_t previous = 0;
    for (std::uint64_t v = 0; v < 5'000'000; v += (v < 1000 ? 1 : v / 97)) {
        const std::size_t i = LatencyHistogram::bucket_index(v);
        EXPECT_LE(LatencyHistogram::bucket_lower(i), v);
        EXPECT_LT(v, LatencyHistogram::bucket_upper(i));
        EXPECT_GE(i, previous);
        previous = i;
        if (v >= 64) {
            const double width = static_cast<double>(LatencyHistogram::bucket_upper(i) -
                                                     LatencyHistogram::bucket_lower(i));
            EXPECT_LE(width / static_cast<double>(LatencyHistogram::bucket_lower(i)), 1.0 / 32.0);
        }
    }
    EXPECT_EQ(LatencyHistogram::bucket_index(63), 63U);
    EXPECT_EQ(LatencyHistogram::bucket_index(64), 64U);
    EXPECT_EQ(LatencyHistogram::bucket_upper(LatencyHistogram::bucket_index(63)),
              LatencyHistogram::bucket_lower(LatencyHistogram::bucket_index(64)));
}

TEST(Histogram, PercentilesAreWithinTwoPercent) {
    LatencyHistogram h;
    for (std::uint64_t v = 1; v <= 100'000; ++v) h.record(v);
    EXPECT_EQ(h.count(), 100'000U);
    EXPECT_NEAR(static_cast<double>(h.percentile(0.50)), 50'000.0, 1'000.0);
    EXPECT_NEAR(static_cast<double>(h.percentile(0.95)), 95'000.0, 1'900.0);
    EXPECT_NEAR(static_cast<double>(h.percentile(0.99)), 99'000.0, 1'980.0);
    EXPECT_EQ(h.percentile(0.0), 1U);
    EXPECT_EQ(LatencyHistogram{}.percentile(0.5), 0U);
}

TEST(Histogram, MergingEqualsRecordingEverything) {
    LatencyHistogram all;
    LatencyHistogram a;
    LatencyHistogram b;
    for (std::uint64_t v = 1; v < 200'000; v = v * 3 / 2 + 1) {
        all.record(v);
        (v % 2 == 0 ? a : b).record(v);
    }
    a.merge(b);
    EXPECT_EQ(a.count(), all.count());
    EXPECT_EQ(a.buckets(), all.buckets());
}

TEST(Histogram, CountsPerDecade) {
    LatencyHistogram h;
    for (std::uint64_t v : {5ULL, 50ULL, 500ULL, 5'000ULL, 50'000ULL, 500'000ULL, 5'000'000ULL,
                            50'000'000ULL, 900'000'000ULL}) {
        h.record(v);
    }
    const auto d = h.decades();
    EXPECT_EQ(d, (std::array<std::uint64_t, 8>{1, 1, 1, 1, 1, 1, 1, 2}));
}

TEST(Summary, TracksCountSumMinMax) {
    stats::Summary<std::uint64_t> s;
    EXPECT_EQ(s.mean(), 0.0);
    s.add(5);
    s.add(1);
    s.add(9);
    stats::Summary<std::uint64_t> t;
    t.add(20);
    s.merge(t);
    s.merge(stats::Summary<std::uint64_t>{});
    EXPECT_EQ(s.count(), 4U);
    EXPECT_EQ(s.sum(), 35U);
    EXPECT_EQ(s.min(), 1U);
    EXPECT_EQ(s.max(), 20U);
    EXPECT_DOUBLE_EQ(s.mean(), 8.75);
}

TEST(QueryClass, AccumulatesMetricsAndTheWorstSample) {
    stats::QueryClass c(1, "select ?", sql::StatementKind::Select);
    auto e1 = event("SELECT 1", 100, "shop", 1705312900);
    auto e2 = event("SELECT 2", 900, "shop", 1705312800);
    auto e3 = event("SELECT 3", 900, "analytics", 1705313000);
    e2.set(log::ExecutionFlag::FullScan);
    c.add(e1);
    c.add(e2);
    c.add(e3);
    EXPECT_EQ(c.calls(), 3U);
    EXPECT_EQ(c.query_time().sum(), 1900U);
    EXPECT_EQ(c.worst().sql, "SELECT 2");
    EXPECT_EQ(c.worst().query_time_us, 900U);
    EXPECT_EQ(c.first_seen(), 1705312800);
    EXPECT_EQ(c.last_seen(), 1705313000);
    EXPECT_EQ(c.flag_count(log::ExecutionFlag::FullScan), 1U);
    EXPECT_NEAR(c.flag_ratio(log::ExecutionFlag::FullScan), 1.0 / 3.0, 1e-9);
    const auto dbs = c.databases();
    ASSERT_EQ(dbs.size(), 2U);
    EXPECT_EQ(dbs[0], (std::pair<std::string, std::uint64_t>{"shop", 2}));
    EXPECT_EQ(c.id_hex(), "0000000000000001");
}

TEST(QueryClass, PercentilesAreClampedToObservedValues) {
    stats::QueryClass c(1, "select ?", sql::StatementKind::Select);
    c.add(event("SELECT 1", 812'345));
    EXPECT_EQ(c.percentile_us(0.5), 812'345U);
    EXPECT_EQ(c.percentile_us(0.99), 812'345U);
}

TEST(QueryClass, MergeKeepsTheEarlierWorstSampleOnTies) {
    stats::QueryClass a(1, "select ?", sql::StatementKind::Select);
    stats::QueryClass b(1, "select ?", sql::StatementKind::Select);
    a.add(event("SELECT 'a'", 500));
    b.add(event("SELECT 'b'", 500));
    a.merge(b);
    EXPECT_EQ(a.worst().sql, "SELECT 'a'");
    stats::QueryClass c(1, "select ?", sql::StatementKind::Select);
    c.add(event("SELECT 'c'", 501));
    a.merge(c);
    EXPECT_EQ(a.worst().sql, "SELECT 'c'");
    EXPECT_EQ(a.calls(), 3U);
}

TEST(Aggregator, GroupsByFingerprint) {
    stats::Aggregator agg;
    agg.add(event("SELECT * FROM t WHERE id = 1", 100));
    agg.add(event("select * from `t` where id = 2", 300));
    agg.add(event("DELETE FROM t WHERE id = 3", 50));
    EXPECT_EQ(agg.class_count(), 2U);
    EXPECT_EQ(agg.totals().events, 3U);
    EXPECT_EQ(agg.totals().query_time_us, 450U);
    const auto id = sql::fingerprint("SELECT * FROM t WHERE id = 1").id;
    ASSERT_NE(agg.find(id), nullptr);
    EXPECT_EQ(agg.find(id)->calls(), 2U);
    EXPECT_EQ(agg.find(12345), nullptr);

    auto classes = agg.take_classes();
    ASSERT_EQ(classes.size(), 2U);
    EXPECT_LT(classes[0].id(), classes[1].id());
    EXPECT_EQ(agg.class_count(), 0U);
}

TEST(Aggregator, ChunkedAggregationIsBitIdenticalToSequential) {
    log::GeneratorOptions options;
    options.events = 8000;
    options.seed = 2024;
    const std::string text = log::SlowLogGenerator(options).generate();

    stats::Aggregator sequential = aggregate(text);
    stats::Aggregator merged;
    for (const auto& chunk : log::split_log(text, 16, 32 * 1024)) {
        merged.merge(aggregate(chunk.text, chunk.initial_database));
    }

    const auto& t1 = sequential.totals();
    const auto& t2 = merged.totals();
    EXPECT_EQ(t1.events, t2.events);
    EXPECT_EQ(t1.query_time_us, t2.query_time_us);
    EXPECT_EQ(t1.rows_examined, t2.rows_examined);
    EXPECT_EQ(t1.first_seen, t2.first_seen);
    EXPECT_EQ(t1.last_seen, t2.last_seen);
    ASSERT_EQ(sequential.class_count(), merged.class_count());
    ASSERT_GE(sequential.class_count(), 18U);

    for (const auto& [id, a] : sequential.classes()) {
        const stats::QueryClass* b = merged.find(id);
        ASSERT_NE(b, nullptr) << a.fingerprint();
        EXPECT_EQ(a.calls(), b->calls());
        EXPECT_EQ(a.query_time().sum(), b->query_time().sum());
        EXPECT_EQ(a.query_time().min(), b->query_time().min());
        EXPECT_EQ(a.query_time().max(), b->query_time().max());
        EXPECT_EQ(a.rows_examined().sum(), b->rows_examined().sum());
        EXPECT_EQ(a.latency().buckets(), b->latency().buckets());
        EXPECT_EQ(a.percentile_us(0.95), b->percentile_us(0.95));
        EXPECT_EQ(a.worst().sql, b->worst().sql);
        EXPECT_EQ(a.worst().timestamp, b->worst().timestamp);
        EXPECT_EQ(a.flag_count(log::ExecutionFlag::FullScan), b->flag_count(log::ExecutionFlag::FullScan));
        EXPECT_EQ(a.databases(), b->databases());
        EXPECT_EQ(a.first_seen(), b->first_seen());
        EXPECT_EQ(a.last_seen(), b->last_seen());
    }
}
