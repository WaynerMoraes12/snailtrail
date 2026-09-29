#include <gtest/gtest.h>

#include "snailtrail/util/hash.hpp"
#include "snailtrail/util/strings.hpp"

namespace util = snailtrail::util;

TEST(Strings, CaseInsensitiveComparison) {
    EXPECT_TRUE(util::iequals("SELECT", "select"));
    EXPECT_TRUE(util::iequals("", ""));
    EXPECT_FALSE(util::iequals("select", "selects"));
    EXPECT_TRUE(util::istarts_with("# User@Host: root", "# user@host:"));
    EXPECT_FALSE(util::istarts_with("#", "# Time"));
}

TEST(Strings, TrimAndSplit) {
    EXPECT_EQ(util::trim("  \t a b \r\n"), "a b");
    EXPECT_EQ(util::trim(""), "");
    const auto parts = util::split("a,,b", ',');
    ASSERT_EQ(parts.size(), 3U);
    EXPECT_EQ(parts[1], "");
    EXPECT_EQ(util::join({"x", "y", "z"}, ", "), "x, y, z");
}

TEST(Strings, ParsesNumbersStrictly) {
    EXPECT_EQ(util::parse_uint("42"), 42U);
    EXPECT_FALSE(util::parse_uint("42x").has_value());
    EXPECT_FALSE(util::parse_uint("").has_value());
    EXPECT_DOUBLE_EQ(*util::parse_double("0.000123"), 0.000123);
    EXPECT_FALSE(util::parse_double("1.5s").has_value());
}

TEST(Strings, FormatsDurationsForHumans) {
    EXPECT_EQ(util::format_duration(0.000812), "812 µs");
    EXPECT_EQ(util::format_duration(0.0456), "45.6 ms");
    EXPECT_EQ(util::format_duration(0.301), "301 ms");
    EXPECT_EQ(util::format_duration(1.234), "1.23 s");
    EXPECT_EQ(util::format_duration(42.0), "42.0 s");
    EXPECT_EQ(util::format_duration(252.0), "4m 12s");
    EXPECT_EQ(util::format_duration(11220.0), "3h 07m");
    EXPECT_EQ(util::format_duration(-1.0), "-");
}

TEST(Strings, FormatsCountsAndBytes) {
    EXPECT_EQ(util::format_count(0), "0");
    EXPECT_EQ(util::format_count(999), "999");
    EXPECT_EQ(util::format_count(1234567), "1,234,567");
    EXPECT_EQ(util::format_count(12000), "12,000");
    EXPECT_EQ(util::format_count(123456), "123,456");
    EXPECT_EQ(util::format_count(1000), "1,000");
    EXPECT_EQ(util::format_compact(12.0), "12");
    EXPECT_EQ(util::format_compact(2.5), "2.5");
    EXPECT_EQ(util::format_compact(12345.0), "12.3k");
    EXPECT_EQ(util::format_compact(4100000.0), "4.1M");
    EXPECT_EQ(util::format_bytes(512), "512 B");
    EXPECT_EQ(util::format_bytes(812ULL * 1024 * 1024 + 400 * 1024), "812.4 MB");
}

TEST(Strings, AbbreviatesOnCodePoints) {
    EXPECT_EQ(util::abbreviate("select   *\n from  t", 100), "select * from t");
    EXPECT_EQ(util::abbreviate("abcdefghij", 5), "abcd…");
    EXPECT_EQ(util::abbreviate("abcde", 5), "abcde");
    EXPECT_EQ(util::abbreviate("ação ação", 4), "açã…");
    EXPECT_EQ(util::display_width("µs → ok"), 7U);
}

TEST(Strings, ConvertsCivilTimeBothWays) {
    EXPECT_EQ(util::to_unix_time(1970, 1, 1, 0, 0, 0), 0);
    EXPECT_EQ(util::to_unix_time(2024, 1, 15, 10, 23, 45), 1705314225);
    EXPECT_EQ(util::format_timestamp(1705314225), "2024-01-15 10:23:45");
    EXPECT_EQ(util::format_timestamp(951782400), "2000-02-29 00:00:00");
}

TEST(Hash, Fnv1aMatchesReferenceVectors) {
    EXPECT_EQ(util::fnv1a_64(""), 0xcbf29ce484222325ULL);
    EXPECT_EQ(util::fnv1a_64("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(util::fnv1a_64("foobar"), 0x85944171f73967e8ULL);
    static_assert(util::fnv1a_64("a") == 0xaf63dc4c8601ec8cULL);
}

TEST(Hash, HexRoundTrip) {
    EXPECT_EQ(util::to_hex(0x5C3E9A1B2C3D4E5FULL), "5C3E9A1B2C3D4E5F");
    EXPECT_EQ(util::to_hex(1), "0000000000000001");
    std::uint64_t value = 0;
    ASSERT_TRUE(util::from_hex("0x5c3e9a1b2c3d4e5f", value));
    EXPECT_EQ(value, 0x5C3E9A1B2C3D4E5FULL);
    EXPECT_FALSE(util::from_hex("xyz", value));
    EXPECT_FALSE(util::from_hex("11112222333344445", value));
}
