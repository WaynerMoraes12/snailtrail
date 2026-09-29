#include "snailtrail/util/json_writer.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace snailtrail::util {

JsonWriter::JsonWriter(std::ostream& out, int indent) : out_(out), indent_(indent) {}

void JsonWriter::newline() {
    if (indent_ <= 0) return;
    out_ << '\n';
    for (std::size_t i = 0; i < stack_.size() * static_cast<std::size_t>(indent_); ++i) out_ << ' ';
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (stack_.empty()) return;
    Frame& top = stack_.back();
    if (top.is_object) throw std::logic_error("JsonWriter: value inside an object needs a key");
    if (!top.empty) out_ << ',';
    top.empty = false;
    newline();
}

JsonWriter& JsonWriter::key(std::string_view name) {
    if (stack_.empty() || !stack_.back().is_object || after_key_) {
        throw std::logic_error("JsonWriter: key outside an object");
    }
    Frame& top = stack_.back();
    if (!top.empty) out_ << ',';
    top.empty = false;
    newline();
    write_string(out_, name);
    out_ << (indent_ > 0 ? ": " : ":");
    after_key_ = true;
    return *this;
}

JsonWriter& JsonWriter::begin_object() {
    before_value();
    out_ << '{';
    stack_.push_back({true});
    return *this;
}

JsonWriter& JsonWriter::end_object() {
    if (stack_.empty() || !stack_.back().is_object) {
        throw std::logic_error("JsonWriter: end_object without begin_object");
    }
    const bool empty = stack_.back().empty;
    stack_.pop_back();
    if (!empty) newline();
    out_ << '}';
    if (stack_.empty() && indent_ > 0) out_ << '\n';
    return *this;
}

JsonWriter& JsonWriter::begin_array() {
    before_value();
    out_ << '[';
    stack_.push_back({false});
    return *this;
}

JsonWriter& JsonWriter::end_array() {
    if (stack_.empty() || stack_.back().is_object) {
        throw std::logic_error("JsonWriter: end_array without begin_array");
    }
    const bool empty = stack_.back().empty;
    stack_.pop_back();
    if (!empty) newline();
    out_ << ']';
    if (stack_.empty() && indent_ > 0) out_ << '\n';
    return *this;
}

JsonWriter& JsonWriter::value(std::string_view s) {
    before_value();
    write_string(out_, s);
    return *this;
}

JsonWriter& JsonWriter::value(bool b) {
    before_value();
    out_ << (b ? "true" : "false");
    return *this;
}

JsonWriter& JsonWriter::value(double d) {
    before_value();
    if (!std::isfinite(d)) {
        out_ << "null";
        return *this;
    }
    std::array<char, 32> buffer{};
    const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), d);
    if (ec != std::errc{}) {
        out_ << "null";
    } else {
        out_.write(buffer.data(), ptr - buffer.data());
    }
    return *this;
}

JsonWriter& JsonWriter::value(std::int64_t n) {
    before_value();
    out_ << n;
    return *this;
}

JsonWriter& JsonWriter::value(std::uint64_t n) {
    before_value();
    out_ << n;
    return *this;
}

JsonWriter& JsonWriter::null() {
    before_value();
    out_ << "null";
    return *this;
}

void JsonWriter::write_string(std::ostream& out, std::string_view s) {
    static constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                const auto u = static_cast<unsigned char>(c);
                out << "\\u00" << hex[u >> 4] << hex[u & 0xF];
            } else {
                out << c;
            }
        }
    }
    out << '"';
}

}
