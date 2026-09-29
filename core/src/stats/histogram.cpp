#include "snailtrail/stats/histogram.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace snailtrail::stats {

std::size_t LatencyHistogram::bucket_index(std::uint64_t micros) noexcept {
    if (micros < 2 * sub_buckets) return static_cast<std::size_t>(micros);
    const auto msb = static_cast<unsigned>(std::bit_width(micros)) - 1U;
    const unsigned shift = msb - sub_bucket_bits;
    return static_cast<std::size_t>((shift + 1) * sub_buckets + ((micros >> shift) - sub_buckets));
}

std::uint64_t LatencyHistogram::bucket_lower(std::size_t index) noexcept {
    if (index < 2 * sub_buckets) return index;
    const auto shift = static_cast<unsigned>(index / sub_buckets - 1);
    const std::uint64_t sub = index % sub_buckets;
    return (sub_buckets + sub) << shift;
}

std::uint64_t LatencyHistogram::bucket_upper(std::size_t index) noexcept {
    if (index < 2 * sub_buckets) return index + 1;
    const auto shift = static_cast<unsigned>(index / sub_buckets - 1);
    return bucket_lower(index) + (std::uint64_t{1} << shift);
}

void LatencyHistogram::record(std::uint64_t micros, std::uint64_t count) {
    const std::size_t index = bucket_index(micros);
    if (index >= counts_.size()) counts_.resize(index + 1, 0);
    counts_[index] += count;
    total_ += count;
}

void LatencyHistogram::merge(const LatencyHistogram& other) {
    if (other.counts_.size() > counts_.size()) counts_.resize(other.counts_.size(), 0);
    for (std::size_t i = 0; i < other.counts_.size(); ++i) counts_[i] += other.counts_[i];
    total_ += other.total_;
}

std::uint64_t LatencyHistogram::percentile(double q) const noexcept {
    if (total_ == 0) return 0;
    q = std::clamp(q, 0.0, 1.0);
    const auto rank = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(total_))));
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        seen += counts_[i];
        if (seen >= rank) {
            const std::uint64_t lower = bucket_lower(i);
            const std::uint64_t width = bucket_upper(i) - lower;
            return lower + (width - 1) / 2;
        }
    }
    return bucket_lower(counts_.size() - 1);
}

std::array<std::uint64_t, LatencyHistogram::decade_count> LatencyHistogram::decades() const noexcept {
    std::array<std::uint64_t, decade_count> out{};
    for (std::size_t i = 0; i < counts_.size(); ++i) {
        if (counts_[i] == 0) continue;
        std::uint64_t value = bucket_lower(i);
        std::size_t decade = 0;
        while (value >= 10 && decade + 1 < decade_count) {
            value /= 10;
            ++decade;
        }
        out[decade] += counts_[i];
    }
    return out;
}

}
