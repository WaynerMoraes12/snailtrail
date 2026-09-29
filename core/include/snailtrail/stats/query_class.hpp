#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "snailtrail/log/query_event.hpp"
#include "snailtrail/sql/statement_kind.hpp"
#include "snailtrail/stats/histogram.hpp"
#include "snailtrail/stats/summary.hpp"

namespace snailtrail::stats {

struct Sample {
    std::string sql;
    std::string database;
    std::string user;
    std::string host;
    std::uint64_t query_time_us = 0;
    std::uint64_t rows_examined = 0;
    std::uint64_t rows_sent = 0;
    std::int64_t timestamp = 0;
};

using Tally = std::vector<std::pair<std::string, std::uint64_t>>;

class QueryClass {
public:
    QueryClass(std::uint64_t id, std::string fingerprint, sql::StatementKind kind);

    void add(const log::QueryEvent& event);
    void merge(const QueryClass& other);

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::string id_hex() const;
    [[nodiscard]] const std::string& fingerprint() const noexcept { return fingerprint_; }
    [[nodiscard]] sql::StatementKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::uint64_t calls() const noexcept { return query_time_.count(); }

    [[nodiscard]] const Summary<std::uint64_t>& query_time() const noexcept { return query_time_; }
    [[nodiscard]] const Summary<std::uint64_t>& lock_time() const noexcept { return lock_time_; }
    [[nodiscard]] const Summary<std::uint64_t>& rows_sent() const noexcept { return rows_sent_; }
    [[nodiscard]] const Summary<std::uint64_t>& rows_examined() const noexcept {
        return rows_examined_;
    }
    [[nodiscard]] const Summary<std::uint64_t>& rows_affected() const noexcept {
        return rows_affected_;
    }
    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }
    [[nodiscard]] const LatencyHistogram& latency() const noexcept { return latency_; }
    [[nodiscard]] std::uint64_t percentile_us(double q) const noexcept;

    [[nodiscard]] std::uint64_t flag_count(log::ExecutionFlag flag) const noexcept;
    [[nodiscard]] double flag_ratio(log::ExecutionFlag flag) const noexcept;

    [[nodiscard]] std::int64_t first_seen() const noexcept { return first_seen_; }
    [[nodiscard]] std::int64_t last_seen() const noexcept { return last_seen_; }
    [[nodiscard]] const Sample& worst() const noexcept { return worst_; }
    [[nodiscard]] Tally databases() const;
    [[nodiscard]] Tally users() const;

private:
    static void count(Tally& tally, std::string_view key, std::uint64_t n);
    static Tally sorted(const Tally& tally);

    std::uint64_t id_;
    std::string fingerprint_;
    sql::StatementKind kind_;
    Summary<std::uint64_t> query_time_;
    Summary<std::uint64_t> lock_time_;
    Summary<std::uint64_t> rows_sent_;
    Summary<std::uint64_t> rows_examined_;
    Summary<std::uint64_t> rows_affected_;
    std::uint64_t bytes_sent_ = 0;
    LatencyHistogram latency_;
    std::array<std::uint64_t, 4> flags_{};
    std::int64_t first_seen_ = 0;
    std::int64_t last_seen_ = 0;
    Sample worst_;
    Tally databases_;
    Tally users_;
};

}
