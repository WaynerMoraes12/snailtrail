#pragma once

#include <cstdint>

namespace snailtrail::stats {

template <typename T>
class Summary {
public:
    constexpr void add(T value) noexcept {
        if (count_ == 0 || value < min_) min_ = value;
        if (count_ == 0 || value > max_) max_ = value;
        sum_ += value;
        ++count_;
    }

    constexpr void merge(const Summary& other) noexcept {
        if (other.count_ == 0) return;
        if (count_ == 0 || other.min_ < min_) min_ = other.min_;
        if (count_ == 0 || other.max_ > max_) max_ = other.max_;
        sum_ += other.sum_;
        count_ += other.count_;
    }

    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] constexpr T sum() const noexcept { return sum_; }
    [[nodiscard]] constexpr T min() const noexcept { return min_; }
    [[nodiscard]] constexpr T max() const noexcept { return max_; }
    [[nodiscard]] constexpr double mean() const noexcept {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_) / static_cast<double>(count_);
    }

private:
    std::uint64_t count_ = 0;
    T sum_{};
    T min_{};
    T max_{};
};

}
