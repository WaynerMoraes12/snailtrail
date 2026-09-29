#pragma once

#include <concepts>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace snailtrail::util {

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out, int indent = 2);

    JsonWriter& begin_object();
    JsonWriter& end_object();
    JsonWriter& begin_array();
    JsonWriter& end_array();

    JsonWriter& key(std::string_view name);

    JsonWriter& value(std::string_view s);
    JsonWriter& value(const char* s) { return value(std::string_view(s)); }
    JsonWriter& value(bool b);
    JsonWriter& value(double d);
    JsonWriter& value(std::int64_t n);
    JsonWriter& value(std::uint64_t n);
    JsonWriter& null();

    template <std::integral T>
        requires(!std::same_as<T, bool> && !std::same_as<T, std::int64_t> &&
                 !std::same_as<T, std::uint64_t>)
    JsonWriter& value(T n) {
        if constexpr (std::is_signed_v<T>) {
            return value(static_cast<std::int64_t>(n));
        } else {
            return value(static_cast<std::uint64_t>(n));
        }
    }

    template <typename T>
    JsonWriter& field(std::string_view name, const T& v) {
        key(name);
        return value(v);
    }

    static void write_string(std::ostream& out, std::string_view s);

private:
    struct Frame {
        bool is_object;
        bool empty = true;
    };

    void before_value();
    void newline();

    std::ostream& out_;
    int indent_;
    std::vector<Frame> stack_;
    bool after_key_ = false;
};

}
