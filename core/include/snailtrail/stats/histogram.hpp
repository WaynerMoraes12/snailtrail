#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace snailtrail::stats {

class LatencyHistogram {
public:
    static constexpr unsigned sub_bucket_bits = 5;
    static constexpr std::uint64_t sub_buckets = std::uint64_t{1} << sub_bucket_bits;
    static constexpr std::size_t decade_count = 8;

    void record(std::uint64_t micros, std::uint64_t count = 1);
    void merge(const LatencyHistogram& other);

    [[nodiscard]] std::uint64_t count() const noexcept { return total_; }
    [[nodiscard]] std::uint64_t percentile(double q) const noexcept;
    [[nodiscard]] std::array<std::uint64_t, decade_count> decades() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& buckets() const noexcept { return counts_; }

    [[nodiscard]] static std::size_t bucket_index(std::uint64_t micros) noexcept;
    [[nodiscard]] static std::uint64_t bucket_lower(std::size_t index) noexcept;
    [[nodiscard]] static std::uint64_t bucket_upper(std::size_t index) noexcept;

private:
    std::vector<std::uint64_t> counts_;
    std::uint64_t total_ = 0;
};

}
