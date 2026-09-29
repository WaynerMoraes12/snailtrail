#include "snailtrail/util/strings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace snailtrail::util {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (to_lower(a[i]) != to_lower(b[i])) return false;
    }
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_lower(c);
    return out;
}

std::string to_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = to_upper(c);
    return out;
}

std::string_view trim_left(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && is_space(s[i])) ++i;
    return s.substr(i);
}

std::string_view trim_right(std::string_view s) noexcept {
    std::size_t n = s.size();
    while (n > 0 && is_space(s[n - 1])) --n;
    return s.substr(0, n);
}

std::string_view trim(std::string_view s) noexcept { return trim_right(trim_left(s)); }

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

std::optional<std::uint64_t> parse_uint(std::string_view s) noexcept {
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || s.empty()) return std::nullopt;
    return value;
}

std::optional<double> parse_double(std::string_view s) noexcept {
    if (s.empty()) return std::nullopt;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return value;
#else
    char buffer[64];
    if (s.size() >= sizeof(buffer)) return std::nullopt;
    std::copy(s.begin(), s.end(), buffer);
    buffer[s.size()] = '\0';
    char* end = nullptr;
    const double value = std::strtod(buffer, &end);
    if (end != buffer + s.size()) return std::nullopt;
    return value;
#endif
}

std::string format_duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "-";
    if (seconds < 1e-3) return std::format("{:.0f} µs", seconds * 1e6);
    if (seconds < 0.1) return std::format("{:.1f} ms", seconds * 1e3);
    if (seconds < 1.0) return std::format("{:.0f} ms", seconds * 1e3);
    if (seconds < 10.0) return std::format("{:.2f} s", seconds);
    if (seconds < 60.0) return std::format("{:.1f} s", seconds);

    const auto total = static_cast<std::uint64_t>(std::llround(seconds));
    if (total < 3600) return std::format("{}m {:02}s", total / 60, total % 60);
    if (total < 86400) return std::format("{}h {:02}m", total / 3600, (total % 3600) / 60);
    return std::format("{}d {:02}h", total / 86400, (total % 86400) / 3600);
}

std::string format_count(std::uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t lead = digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (i - lead) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string format_compact(double n) {
    if (!std::isfinite(n)) return "-";
    if (n < 1000.0) {
        if (n == std::floor(n)) return std::format("{:.0f}", n);
        return n < 10.0 ? std::format("{:.1f}", n) : std::format("{:.0f}", n);
    }
    static constexpr const char* suffixes[] = {"k", "M", "B", "T"};
    double value = n;
    int index = -1;
    while (value >= 1000.0 && index < 3) {
        value /= 1000.0;
        ++index;
    }
    return value < 100.0 ? std::format("{:.1f}{}", value, suffixes[index])
                         : std::format("{:.0f}{}", value, suffixes[index]);
}

std::string format_bytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", value, units[unit]);
}

std::string abbreviate(std::string_view s, std::size_t max_chars) {
    std::string collapsed;
    collapsed.reserve(s.size());
    for (char c : trim(s)) {
        if (is_space(c)) {
            if (collapsed.back() != ' ') collapsed += ' ';
        } else {
            collapsed += c;
        }
    }
    if (max_chars == 0 || display_width(collapsed) <= max_chars) return collapsed;

    std::size_t kept = 0;
    std::size_t cut = 0;
    for (; cut < collapsed.size(); ++cut) {
        if ((static_cast<unsigned char>(collapsed[cut]) & 0xC0) != 0x80) {
            if (kept == max_chars - 1) break;
            ++kept;
        }
    }
    return std::string(trim_right(std::string_view(collapsed).substr(0, cut))) + "…";
}

std::size_t display_width(std::string_view s) noexcept {
    std::size_t width = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++width;
    }
    return width;
}

namespace {

constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

struct Civil {
    std::int64_t year;
    unsigned month;
    unsigned day;
};

constexpr Civil civil_from_days(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {y + (m <= 2 ? 1 : 0), m, d};
}

}

std::int64_t to_unix_time(int year, int month, int day, int hour, int minute,
                          int second) noexcept {
    return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) *
               86400 +
           hour * 3600 + minute * 60 + second;
}

std::string format_timestamp(std::int64_t unix_seconds) {
    std::int64_t days = unix_seconds / 86400;
    std::int64_t rem = unix_seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    const Civil c = civil_from_days(days);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", c.year, c.month, c.day, rem / 3600,
                       (rem % 3600) / 60, rem % 60);
}

}
