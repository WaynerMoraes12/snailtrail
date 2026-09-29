#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snailtrail::util {

constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char to_upper(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool iequals(std::string_view a, std::string_view b) noexcept;
bool istarts_with(std::string_view s, std::string_view prefix) noexcept;

std::string to_lower(std::string_view s);
std::string to_upper(std::string_view s);

std::string_view trim(std::string_view s) noexcept;
std::string_view trim_left(std::string_view s) noexcept;
std::string_view trim_right(std::string_view s) noexcept;

std::vector<std::string_view> split(std::string_view s, char sep);
std::string join(const std::vector<std::string>& parts, std::string_view sep);

std::optional<std::uint64_t> parse_uint(std::string_view s) noexcept;
std::optional<double> parse_double(std::string_view s) noexcept;

std::string format_duration(double seconds);
std::string format_count(std::uint64_t n);
std::string format_compact(double n);
std::string format_bytes(std::uint64_t bytes);

std::string abbreviate(std::string_view s, std::size_t max_chars);
std::size_t display_width(std::string_view s) noexcept;

std::string format_timestamp(std::int64_t unix_seconds);
std::int64_t to_unix_time(int year, int month, int day, int hour, int minute, int second) noexcept;

}
