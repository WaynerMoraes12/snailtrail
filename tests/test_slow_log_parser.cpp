#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "snailtrail/log/slow_log_parser.hpp"

using namespace snailtrail::log;

namespace {

struct Captured {
    std::string sql;
    std::string database;
    std::string user;
    std::string host;
    std::uint64_t query_time_us;
    std::uint64_t lock_time_us;
    std::uint64_t rows_sent;
    std::uint64_t rows_examined;
    std::uint64_t rows_affected;
    std::uint64_t bytes_sent;
    std::uint64_t thread_id;
    std::int64_t timestamp;
    std::uint8_t flags;
};

std::vector<Captured> parse_all(std::string_view text, SlowLogParser::Counters* counters = nullptr) {
    std::vector<Captured> out;
    const auto c = SlowLogParser::parse_text(text, [&](const QueryEvent& e) {
        out.push_back({std::string(e.sql), std::string(e.database), std::string(e.user),
                       std::string(e.host), e.query_time_us, e.lock_time_us, e.rows_sent,
                       e.rows_examined, e.rows_affected, e.bytes_sent, e.thread_id, e.timestamp,
                       e.flags});
    });
    if (counters != nullptr) *counters = c;
    return out;
}

bool has(std::uint8_t flags, ExecutionFlag f) { return (flags & static_cast<std::uint8_t>(f)) != 0; }

constexpr std::string_view mysql8 = R"(/usr/sbin/mysqld, Version: 8.4.2 (MySQL Community Server - GPL). started with:
Tcp port: 3306  Unix socket: /var/run/mysqld/mysqld.sock
Time                 Id Command    Argument
# Time: 2024-01-15T10:23:45.123456Z
# User@Host: app[app] @ app-1 [10.0.0.12]  Id:    42
# Query_time: 0.045123  Lock_time: 0.000004 Rows_sent: 20  Rows_examined: 3120 Thread_id: 42 Errno: 0 Killed: 0 Bytes_received: 0 Bytes_sent: 1432 Read_first: 0 Read_last: 0 Read_key: 1 Read_next: 0 Read_prev: 0 Read_rnd: 0 Read_rnd_next: 0 Sort_merge_passes: 0 Sort_range_count: 0 Sort_rows: 20 Sort_scan_count: 1 Created_tmp_disk_tables: 0 Created_tmp_tables: 0 Count_hit_tmp_table_size: 0 Start: 2024-01-15T10:23:45.078333Z End: 2024-01-15T10:23:45.123456Z
use shop;
SET timestamp=1705314225;
SELECT id, total
FROM orders
# picked by the dashboard
WHERE customer_id = 812 AND status = 'paid'
ORDER BY created_at DESC LIMIT 20;
# Time: 2024-01-15T10:23:46.000100Z
# User@Host: root[root] @ localhost []  Id:     8
# Query_time: 0.000004  Lock_time: 0.000000 Rows_sent: 0  Rows_examined: 0
SET timestamp=1705314226;
# administrator command: Quit;
# Time: 2024-01-15T10:23:47.500000Z
# User@Host: report[report] @  [172.18.0.5]  Id:    51
# Query_time: 2.500000  Lock_time: 0.000120 Rows_sent: 30  Rows_examined: 2000000 Thread_id: 51 Errno: 0 Killed: 0 Bytes_received: 0 Bytes_sent: 900 Read_first: 1 Read_last: 0 Read_key: 0 Read_next: 0 Read_prev: 0 Read_rnd: 0 Read_rnd_next: 2000001 Sort_merge_passes: 0 Sort_range_count: 0 Sort_rows: 0 Sort_scan_count: 0 Created_tmp_disk_tables: 1 Created_tmp_tables: 1 Count_hit_tmp_table_size: 0 Start: 2024-01-15T10:23:45.000000Z End: 2024-01-15T10:23:47.500000Z
SET timestamp=1705314225;
SELECT DATE(created_at), SUM(total) FROM orders GROUP BY DATE(created_at);
)";

}

TEST(SlowLog, ParsesMicrosecondsExactly) {
    EXPECT_EQ(parse_microseconds("0.000123"), 123U);
    EXPECT_EQ(parse_microseconds("12.5"), 12'500'000U);
    EXPECT_EQ(parse_microseconds("3"), 3'000'000U);
    EXPECT_EQ(parse_microseconds(".25"), 250'000U);
    EXPECT_EQ(parse_microseconds("0.0000015"), 2U);
    EXPECT_EQ(parse_microseconds("0.0000014"), 1U);
    EXPECT_FALSE(parse_microseconds("").has_value());
    EXPECT_FALSE(parse_microseconds(".").has_value());
    EXPECT_FALSE(parse_microseconds("1.2s").has_value());
}

TEST(SlowLog, ParsesEveryTimeFormat) {
    EXPECT_EQ(parse_log_time("2024-01-15T10:23:45.123456Z"), 1705314225);
    EXPECT_EQ(parse_log_time("2024-01-15T07:23:45.123456-03:00"), 1705314225);
    EXPECT_EQ(parse_log_time("240115 10:23:45"), 1705314225);
    EXPECT_EQ(parse_log_time("240115  9:05:01"), 1705309501);
    EXPECT_FALSE(parse_log_time("yesterday").has_value());
}

TEST(SlowLog, ReadsMySql8WithSlowLogExtra) {
    SlowLogParser::Counters counters;
    const auto events = parse_all(mysql8, &counters);
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(counters.events, 2U);
    EXPECT_EQ(counters.skipped, 1U);

    const Captured& first = events[0];
    EXPECT_EQ(first.sql, "SELECT id, total\nFROM orders\n# picked by the dashboard\n"
                         "WHERE customer_id = 812 AND status = 'paid'\n"
                         "ORDER BY created_at DESC LIMIT 20");
    EXPECT_EQ(first.database, "shop");
    EXPECT_EQ(first.user, "app");
    EXPECT_EQ(first.host, "app-1");
    EXPECT_EQ(first.thread_id, 42U);
    EXPECT_EQ(first.query_time_us, 45123U);
    EXPECT_EQ(first.lock_time_us, 4U);
    EXPECT_EQ(first.rows_sent, 20U);
    EXPECT_EQ(first.rows_examined, 3120U);
    EXPECT_EQ(first.bytes_sent, 1432U);
    EXPECT_EQ(first.timestamp, 1705314225);
    EXPECT_TRUE(has(first.flags, ExecutionFlag::Filesort));
    EXPECT_FALSE(has(first.flags, ExecutionFlag::FullScan));

    const Captured& report = events[1];
    EXPECT_EQ(report.database, "shop");
    EXPECT_EQ(report.host, "172.18.0.5");
    EXPECT_EQ(report.query_time_us, 2'500'000U);
    EXPECT_TRUE(has(report.flags, ExecutionFlag::FullScan));
    EXPECT_TRUE(has(report.flags, ExecutionFlag::TmpTable));
    EXPECT_TRUE(has(report.flags, ExecutionFlag::TmpTableOnDisk));
    EXPECT_FALSE(has(report.flags, ExecutionFlag::Filesort));
}

TEST(SlowLog, ReadsPerconaServer) {
    const auto events = parse_all(R"(# Time: 2024-01-15T10:23:45.123456Z
# User@Host: app[app] @ web-1 [10.0.0.21]  Id:    17
# Schema: shop  Last_errno: 0  Killed: 0
# Query_time: 0.142311  Lock_time: 0.000044  Rows_sent: 1  Rows_examined: 200000  Rows_affected: 0  Bytes_sent: 72
# Stored_routine:
# Tmp_tables: 0  Tmp_disk_tables: 0  Tmp_table_sizes: 0
# Full_scan: Yes  Full_join: No  Tmp_table: No  Tmp_table_on_disk: No
# Filesort: No  Filesort_on_disk: No  Merge_passes: 0
#   InnoDB_IO_r_ops: 0  InnoDB_IO_r_bytes: 0  InnoDB_IO_r_wait: 0.000000
#   InnoDB_pages_distinct: 812
SET timestamp=1705314225;
SELECT id, full_name FROM customers WHERE phone = 5511912345678;
)");
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].database, "shop");
    EXPECT_EQ(events[0].rows_examined, 200000U);
    EXPECT_EQ(events[0].bytes_sent, 72U);
    EXPECT_TRUE(has(events[0].flags, ExecutionFlag::FullScan));
    EXPECT_FALSE(has(events[0].flags, ExecutionFlag::TmpTable));
}

TEST(SlowLog, ReadsMariaDbWithoutTimeLines) {
    const auto events = parse_all(R"(# Time: 240115 10:23:45
# User@Host: app[app] @ web-2 [10.0.0.22]
# Thread_id: 31  Schema: shop  QC_hit: No
# Query_time: 0.052000  Lock_time: 0.000100  Rows_sent: 20  Rows_examined: 3120
# Rows_affected: 0  Bytes_sent: 1432
# Full_scan: No  Full_join: No  Tmp_table: No  Tmp_table_on_disk: No
# Filesort: Yes  Filesort_on_disk: No  Merge_passes: 0  Priority_queue: Yes
SET timestamp=1705314225;
SELECT id FROM orders WHERE customer_id = 812;
# User@Host: app[app] @ web-2 [10.0.0.22]
# Thread_id: 32  Schema:   QC_hit: No
# Query_time: 1.000000  Lock_time: 0.000000  Rows_sent: 0  Rows_examined: 0
# Rows_affected: 3  Bytes_sent: 52
SET timestamp=1705314225;
UPDATE orders SET status = 'paid' WHERE id IN (1, 2, 3);
)");
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].thread_id, 31U);
    EXPECT_EQ(events[0].database, "shop");
    EXPECT_TRUE(has(events[0].flags, ExecutionFlag::Filesort));
    EXPECT_EQ(events[1].thread_id, 32U);
    EXPECT_EQ(events[1].rows_affected, 3U);
    EXPECT_EQ(events[1].timestamp, 1705314225);
    EXPECT_EQ(events[1].sql, "UPDATE orders SET status = 'paid' WHERE id IN (1, 2, 3)");
}

TEST(SlowLog, KeepsTheDatabaseAcrossEventsUntilTheNextUse) {
    const auto events = parse_all(R"(# Query_time: 1.0  Lock_time: 0 Rows_sent: 0  Rows_examined: 0
use `shop`;
SELECT 1;
# Query_time: 1.0  Lock_time: 0 Rows_sent: 0  Rows_examined: 0
SELECT 2;
# Query_time: 1.0  Lock_time: 0 Rows_sent: 0  Rows_examined: 0
use analytics;
SELECT 3;
)");
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].database, "shop");
    EXPECT_EQ(events[1].database, "shop");
    EXPECT_EQ(events[2].database, "analytics");
}

TEST(SlowLog, SurvivesRestartsCrlfAndGarbage) {
    const std::string text =
        "garbage before anything\r\n"
        "# Time: 2024-01-15T10:00:00.000000Z\r\n"
        "# User@Host: a[a] @ h []  Id: 1\r\n"
        "# Query_time: 0.5  Lock_time: 0.0 Rows_sent: 1  Rows_examined: 1\r\n"
        "SELECT 'before restart';\r\n"
        "/usr/sbin/mysqld, Version: 8.4.2 (MySQL Community Server - GPL). started with:\r\n"
        "Tcp port: 3306  Unix socket: /var/run/mysqld/mysqld.sock\r\n"
        "Time                 Id Command    Argument\r\n"
        "# Time: 2024-01-15T10:05:00.000000Z\r\n"
        "# User@Host: a[a] @ h []  Id: 2\r\n"
        "# Query_time: 0.25  Lock_time: 0.0 Rows_sent: 1  Rows_examined: 1\r\n"
        "SELECT 'after restart';\r\n"
        "# Time: 2024-01-15T10:06:00.000000Z\r\n"
        "# User@Host: a[a] @ h []  Id: 3\r\n";
    SlowLogParser::Counters counters;
    const auto events = parse_all(text, &counters);
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].sql, "SELECT 'before restart'");
    EXPECT_EQ(events[1].sql, "SELECT 'after restart'");
    EXPECT_EQ(events[1].timestamp, 1705313100);
    EXPECT_EQ(counters.lines, 14U);
}

TEST(SlowLog, IncrementalFeedingMatchesParseText) {
    std::vector<std::string> incremental;
    SlowLogParser parser([&](const QueryEvent& e) { incremental.emplace_back(e.sql); });
    for_each_line(mysql8, [&](std::string_view line) { parser.feed(line); });
    parser.finish();
    std::vector<std::string> batch;
    for (const auto& e : parse_all(mysql8)) batch.push_back(e.sql);
    EXPECT_EQ(incremental, batch);
    parser.finish();
    EXPECT_EQ(parser.counters().events, 2U);
}

TEST(SlowLog, ForEachLineHandlesEdges) {
    std::vector<std::string> lines;
    for_each_line("a\n\nb", [&](std::string_view l) { lines.emplace_back(l); });
    EXPECT_EQ(lines, (std::vector<std::string>{"a", "", "b"}));
    lines.clear();
    for_each_line("", [&](std::string_view l) { lines.emplace_back(l); });
    EXPECT_TRUE(lines.empty());
}
