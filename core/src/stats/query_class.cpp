#include "snailtrail/stats/query_class.hpp"

#include <algorithm>

#include "snailtrail/util/hash.hpp"

namespace snailtrail::stats {

namespace {

constexpr std::size_t flag_slot(log::ExecutionFlag flag) noexcept {
    switch (flag) {
    case log::ExecutionFlag::FullScan: return 0;
    case log::ExecutionFlag::Filesort: return 1;
    case log::ExecutionFlag::TmpTable: return 2;
    case log::ExecutionFlag::TmpTableOnDisk: return 3;
    }
    return 0;
}

constexpr log::ExecutionFlag all_flags[] = {log::ExecutionFlag::FullScan, log::ExecutionFlag::Filesort,
                                            log::ExecutionFlag::TmpTable,
                                            log::ExecutionFlag::TmpTableOnDisk};

}

QueryClass::QueryClass(std::uint64_t id, std::string fingerprint, sql::StatementKind kind)
    : id_(id), fingerprint_(std::move(fingerprint)), kind_(kind) {}

std::string QueryClass::id_hex() const { return util::to_hex(id_); }

void QueryClass::count(Tally& tally, std::string_view key, std::uint64_t n) {
    for (auto& [name, total] : tally) {
        if (name == key) {
            total += n;
            return;
        }
    }
    tally.emplace_back(std::string(key), n);
}

Tally QueryClass::sorted(const Tally& tally) {
    Tally out = tally;
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return out;
}

void QueryClass::add(const log::QueryEvent& e) {
    const bool first = calls() == 0;
    query_time_.add(e.query_time_us);
    lock_time_.add(e.lock_time_us);
    rows_sent_.add(e.rows_sent);
    rows_examined_.add(e.rows_examined);
    rows_affected_.add(e.rows_affected);
    bytes_sent_ += e.bytes_sent;
    latency_.record(e.query_time_us);
    for (auto flag : all_flags) {
        if (e.has(flag)) ++flags_[flag_slot(flag)];
    }
    if (e.timestamp != 0) {
        if (first_seen_ == 0 || e.timestamp < first_seen_) first_seen_ = e.timestamp;
        if (e.timestamp > last_seen_) last_seen_ = e.timestamp;
    }
    count(databases_, e.database, 1);
    count(users_, e.user, 1);

    if (first || e.query_time_us > worst_.query_time_us) {
        worst_.sql.assign(e.sql);
        worst_.database.assign(e.database);
        worst_.user.assign(e.user);
        worst_.host.assign(e.host);
        worst_.query_time_us = e.query_time_us;
        worst_.rows_examined = e.rows_examined;
        worst_.rows_sent = e.rows_sent;
        worst_.timestamp = e.timestamp;
    }
}

void QueryClass::merge(const QueryClass& other) {
    if (other.calls() == 0) return;
    const bool empty = calls() == 0;
    query_time_.merge(other.query_time_);
    lock_time_.merge(other.lock_time_);
    rows_sent_.merge(other.rows_sent_);
    rows_examined_.merge(other.rows_examined_);
    rows_affected_.merge(other.rows_affected_);
    bytes_sent_ += other.bytes_sent_;
    latency_.merge(other.latency_);
    for (std::size_t i = 0; i < flags_.size(); ++i) flags_[i] += other.flags_[i];
    if (other.first_seen_ != 0 && (first_seen_ == 0 || other.first_seen_ < first_seen_)) {
        first_seen_ = other.first_seen_;
    }
    last_seen_ = std::max(last_seen_, other.last_seen_);
    for (const auto& [name, n] : other.databases_) count(databases_, name, n);
    for (const auto& [name, n] : other.users_) count(users_, name, n);
    if (empty || other.worst_.query_time_us > worst_.query_time_us) worst_ = other.worst_;
}

void QueryClass::rename_database(std::string_view from, std::string_view to) {
    const auto it = std::find_if(databases_.begin(), databases_.end(),
                                 [&](const auto& entry) { return entry.first == from; });
    if (it != databases_.end()) {
        const std::uint64_t n = it->second;
        databases_.erase(it);
        count(databases_, to, n);
    }
    if (worst_.database == from) worst_.database = to;
}

std::uint64_t QueryClass::percentile_us(double q) const noexcept {
    if (calls() == 0) return 0;
    return std::clamp(latency_.percentile(q), query_time_.min(), query_time_.max());
}

std::uint64_t QueryClass::flag_count(log::ExecutionFlag flag) const noexcept {
    return flags_[flag_slot(flag)];
}

double QueryClass::flag_ratio(log::ExecutionFlag flag) const noexcept {
    return calls() == 0 ? 0.0
                        : static_cast<double>(flag_count(flag)) / static_cast<double>(calls());
}

Tally QueryClass::databases() const { return sorted(databases_); }

Tally QueryClass::users() const { return sorted(users_); }

}
