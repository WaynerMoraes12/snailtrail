#include "snailtrail/stats/aggregator.hpp"

#include <algorithm>

namespace snailtrail::stats {

void Totals::add(const log::QueryEvent& e) noexcept {
    ++events;
    query_time_us += e.query_time_us;
    lock_time_us += e.lock_time_us;
    rows_sent += e.rows_sent;
    rows_examined += e.rows_examined;
    rows_affected += e.rows_affected;
    if (e.timestamp != 0) {
        if (first_seen == 0 || e.timestamp < first_seen) first_seen = e.timestamp;
        last_seen = std::max(last_seen, e.timestamp);
    }
}

void Totals::merge(const Totals& o) noexcept {
    events += o.events;
    query_time_us += o.query_time_us;
    lock_time_us += o.lock_time_us;
    rows_sent += o.rows_sent;
    rows_examined += o.rows_examined;
    rows_affected += o.rows_affected;
    if (o.first_seen != 0 && (first_seen == 0 || o.first_seen < first_seen)) first_seen = o.first_seen;
    last_seen = std::max(last_seen, o.last_seen);
}

void Aggregator::add(const log::QueryEvent& event) {
    fingerprinter_.compute_into(event.sql, scratch_);
    auto it = classes_.find(scratch_.id);
    if (it == classes_.end()) {
        it = classes_.emplace(scratch_.id, QueryClass(scratch_.id, scratch_.text, scratch_.kind)).first;
    }
    it->second.add(event);
    totals_.add(event);
}

void Aggregator::merge(Aggregator&& other) {
    for (auto& [id, cls] : other.classes_) {
        auto it = classes_.find(id);
        if (it == classes_.end()) {
            classes_.emplace(id, std::move(cls));
        } else {
            it->second.merge(cls);
        }
    }
    other.classes_.clear();
    totals_.merge(other.totals_);
    other.totals_ = Totals{};
}

void Aggregator::rename_database(std::string_view from, std::string_view to) {
    for (auto& [id, cls] : classes_) cls.rename_database(from, to);
}

const QueryClass* Aggregator::find(std::uint64_t id) const {
    const auto it = classes_.find(id);
    return it == classes_.end() ? nullptr : &it->second;
}

std::vector<QueryClass> Aggregator::take_classes() {
    std::vector<QueryClass> out;
    out.reserve(classes_.size());
    for (auto& [id, cls] : classes_) out.push_back(std::move(cls));
    classes_.clear();
    std::sort(out.begin(), out.end(),
              [](const QueryClass& a, const QueryClass& b) { return a.id() < b.id(); });
    return out;
}

}
