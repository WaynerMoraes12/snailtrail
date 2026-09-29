#include <gtest/gtest.h>

#include <limits>
#include <sstream>

#include "snailtrail/util/json_writer.hpp"

using snailtrail::util::JsonWriter;

TEST(JsonWriter, WritesCompactNestedStructures) {
    std::ostringstream out;
    JsonWriter w(out, 0);
    w.begin_object()
        .field("name", "snailtrail")
        .field("events", 1200)
        .field("ratio", 0.5)
        .field("ok", true)
        .key("tags")
        .begin_array()
        .value("a")
        .value(std::uint64_t{7})
        .null()
        .end_array()
        .key("empty")
        .begin_object()
        .end_object()
        .end_object();
    EXPECT_EQ(out.str(),
              R"({"name":"snailtrail","events":1200,"ratio":0.5,"ok":true,"tags":["a",7,null],"empty":{}})");
}

TEST(JsonWriter, IndentsWhenAsked) {
    std::ostringstream out;
    JsonWriter w(out, 2);
    w.begin_object().field("a", 1).key("b").begin_array().value(2).end_array().end_object();
    EXPECT_EQ(out.str(), "{\n  \"a\": 1,\n  \"b\": [\n    2\n  ]\n}\n");
}

TEST(JsonWriter, EscapesStrings) {
    std::ostringstream out;
    JsonWriter::write_string(out, "quote\" slash\\ nl\n tab\t bell\x07 µ");
    EXPECT_EQ(out.str(), R"("quote\" slash\\ nl\n tab\t bell\u0007 µ")");
}

TEST(JsonWriter, NonFiniteNumbersBecomeNull) {
    std::ostringstream out;
    JsonWriter w(out, 0);
    w.begin_array()
        .value(std::numeric_limits<double>::infinity())
        .value(std::numeric_limits<double>::quiet_NaN())
        .end_array();
    EXPECT_EQ(out.str(), "[null,null]");
}

TEST(JsonWriter, RejectsMisuse) {
    std::ostringstream out;
    JsonWriter w(out, 0);
    w.begin_object();
    EXPECT_THROW(w.value(1), std::logic_error);
    EXPECT_THROW(w.end_array(), std::logic_error);
}
