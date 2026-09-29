#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "snailtrail/log/query_event.hpp"
#include "snailtrail/sql/fingerprint.hpp"
#include "snailtrail/stats/query_class.hpp"

namespace snailtrail::stats {

struct Totals {
    std::uint64_t events = 0;
    std::uint64_t query_time_us = 0;
    std::uint64_t lock_time_us = 0;
    std::uint64_t rows_sent = 0;
    std::uint64_t rows_examined = 0;
    std::uint64_t rows_affected = 0;
    std::int64_t first_seen = 0;
    std::int64_t last_seen = 0;

    void add(const log::QueryEvent& event) noexcept;
    void merge(const Totals& other) noexcept;
};

class Aggregator {
public:
    void add(const log::QueryEvent& event);
    void merge(Aggregator&& other);
    void rename_database(std::string_view from, std::string_view to);

    [[nodiscard]] const Totals& totals() const noexcept { return totals_; }
    [[nodiscard]] std::size_t class_count() const noexcept { return classes_.size(); }
    [[nodiscard]] const QueryClass* find(std::uint64_t id) const;
    [[nodiscard]] const std::unordered_map<std::uint64_t, QueryClass>& classes() const noexcept {
        return classes_;
    }

    std::vector<QueryClass> take_classes();

private:
    sql::Fingerprinter fingerprinter_;
    sql::Fingerprint scratch_;
    std::unordered_map<std::uint64_t, QueryClass> classes_;
    Totals totals_;
};

}
