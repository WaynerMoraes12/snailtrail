#include "snailtrail/log/slow_log_parser.hpp"

#include "snailtrail/util/strings.hpp"

namespace snailtrail::log {

std::optional<std::uint64_t> parse_microseconds(std::string_view seconds) noexcept {
    seconds = util::trim(seconds);
    if (seconds.empty()) return std::nullopt;
    std::uint64_t whole = 0;
    std::size_t i = 0;
    std::size_t whole_digits = 0;
    for (; i < seconds.size() && util::is_digit(seconds[i]); ++i, ++whole_digits) {
        whole = whole * 10 + static_cast<std::uint64_t>(seconds[i] - '0');
    }
    std::uint64_t fraction = 0;
    int digits = 0;
    bool round_up = false;
    if (i < seconds.size() && seconds[i] == '.') {
        for (++i; i < seconds.size() && util::is_digit(seconds[i]); ++i) {
            if (digits < 6) {
                fraction = fraction * 10 + static_cast<std::uint64_t>(seconds[i] - '0');
                ++digits;
            } else if (digits == 6) {
                round_up = seconds[i] >= '5';
                ++digits;
            }
        }
    }
    if (i != seconds.size() || (whole_digits == 0 && digits == 0)) return std::nullopt;
    for (; digits < 6; ++digits) fraction *= 10;
    return whole * 1'000'000 + fraction + (round_up ? 1 : 0);
}

namespace {

int two_digits(std::string_view s, std::size_t at) noexcept {
    if (at + 2 > s.size()) return -1;
    const char a = s[at] == ' ' ? '0' : s[at];
    const char b = s[at + 1];
    if (!util::is_digit(a) || !util::is_digit(b)) return -1;
    return (a - '0') * 10 + (b - '0');
}

std::string_view unquote_name(std::string_view name) noexcept {
    name = util::trim(name);
    if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
        name = name.substr(1, name.size() - 2);
    }
    return name;
}

bool yes(std::string_view value) noexcept { return util::iequals(value, "yes"); }

bool positive(std::string_view value) noexcept {
    const auto n = util::parse_uint(value);
    return n.has_value() && *n > 0;
}

}

std::optional<std::int64_t> parse_log_time(std::string_view text) noexcept {
    text = util::trim(text);
    if (text.size() >= 19 && text[4] == '-' && text[7] == '-' && text[10] == 'T') {
        const auto year = util::parse_uint(text.substr(0, 4));
        const int month = two_digits(text, 5);
        const int day = two_digits(text, 8);
        const int hour = two_digits(text, 11);
        const int minute = two_digits(text, 14);
        const int second = two_digits(text, 17);
        if (!year || month < 1 || day < 1 || hour < 0 || minute < 0 || second < 0) {
            return std::nullopt;
        }
        std::int64_t t = util::to_unix_time(static_cast<int>(*year), month, day, hour, minute, second);
        std::size_t i = 19;
        while (i < text.size() && (text[i] == '.' || util::is_digit(text[i]))) ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-') && i + 6 <= text.size()) {
            const int oh = two_digits(text, i + 1);
            const int om = two_digits(text, i + 4);
            if (oh >= 0 && om >= 0) {
                const std::int64_t offset = oh * 3600 + om * 60;
                t += text[i] == '+' ? -offset : offset;
            }
        }
        return t;
    }
    if (text.size() >= 14 && text[6] == ' ') {
        const int yy = two_digits(text, 0);
        const int month = two_digits(text, 2);
        const int day = two_digits(text, 4);
        const std::string_view trimmed = util::trim(text.substr(7));
        const std::string clock =
            trimmed.size() == 7 ? "0" + std::string(trimmed) : std::string(trimmed);
        const int hour = two_digits(clock, 0);
        const int minute = two_digits(clock, 3);
        const int second = two_digits(clock, 6);
        if (yy < 0 || month < 1 || day < 1 || hour < 0 || minute < 0 || second < 0) {
            return std::nullopt;
        }
        return util::to_unix_time(2000 + yy, month, day, hour, minute, second);
    }
    return std::nullopt;
}

SlowLogParser::SlowLogParser(EventHandler handler) : handler_(std::move(handler)) {}

void SlowLogParser::set_database(std::string_view database) { database_ = database; }

void SlowLogParser::start_event() {
    if (state_ == State::Body) emit();
    event_ = QueryEvent{};
    sql_.clear();
    event_schema_.clear();
    user_.clear();
    host_.clear();
    admin_command_ = false;
    state_ = State::Header;
}

void SlowLogParser::emit() {
    state_ = State::Idle;
    std::string_view sql = util::trim(sql_);
    while (!sql.empty() && (sql.back() == ';' || util::is_space(sql.back()))) sql.remove_suffix(1);
    if (admin_command_ || sql.empty()) {
        ++counters_.skipped;
        return;
    }
    event_.sql = sql;
    event_.database = event_schema_.empty() ? database_ : event_schema_;
    event_.user = user_;
    event_.host = host_;
    ++counters_.events;
    handler_(event_);
}

void SlowLogParser::finish() {
    if (state_ == State::Body) emit();
    state_ = State::Idle;
}

bool SlowLogParser::is_server_banner(std::string_view line) noexcept {
    return line.ends_with("started with:") || util::istarts_with(line, "tcp port:") ||
           (line.starts_with("Time ") && line.find("Id Command") != std::string_view::npos);
}

void SlowLogParser::parse_user_host(std::string_view rest) {
    const std::string_view s = util::trim(rest);
    const std::size_t at = s.find(" @");
    const std::size_t bracket = s.find('[');
    user_ = util::trim(s.substr(0, std::min(bracket, at)));

    if (at == std::string_view::npos) return;
    std::string_view host = util::trim(s.substr(at + 2));
    const std::size_t id = host.find("Id:");
    if (id != std::string_view::npos) {
        if (auto n = util::parse_uint(util::trim(host.substr(id + 3)))) event_.thread_id = *n;
        host = util::trim(host.substr(0, id));
    }
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        host_ = host.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1);
    } else {
        host_ = host.substr(0, host.find_first_of(" ["));
    }
}

void SlowLogParser::parse_attributes(std::string_view rest) {
    std::size_t pos = 0;
    auto next_token = [&]() -> std::string_view {
        while (pos < rest.size() && util::is_space(rest[pos])) ++pos;
        const std::size_t start = pos;
        while (pos < rest.size() && !util::is_space(rest[pos])) ++pos;
        return rest.substr(start, pos - start);
    };

    while (true) {
        const std::string_view token = next_token();
        if (token.empty()) return;
        if (token.size() < 2 || !token.ends_with(':')) continue;
        const std::string_view key = token.substr(0, token.size() - 1);

        const std::size_t saved = pos;
        std::string_view value = next_token();
        if (value.ends_with(':')) {
            pos = saved;
            value = {};
        }

        apply_attribute(key, value);
    }
}

void SlowLogParser::apply_attribute(std::string_view key, std::string_view value) {
    switch (key.front()) {
    case 'Q':
        if (key == "Query_time") {
            if (auto us = parse_microseconds(value)) event_.query_time_us = *us;
        }
        return;
    case 'L':
        if (key == "Lock_time") {
            if (auto us = parse_microseconds(value)) event_.lock_time_us = *us;
        }
        return;
    case 'R':
        if (key == "Rows_sent") {
            event_.rows_sent = util::parse_uint(value).value_or(0);
        } else if (key == "Rows_examined") {
            event_.rows_examined = util::parse_uint(value).value_or(0);
        } else if (key == "Rows_affected") {
            event_.rows_affected = util::parse_uint(value).value_or(0);
        } else if (key == "Read_rnd_next" && positive(value)) {
            event_.set(ExecutionFlag::FullScan);
        }
        return;
    case 'B':
        if (key == "Bytes_sent") event_.bytes_sent = util::parse_uint(value).value_or(0);
        return;
    case 'T':
        if (key == "Thread_id") {
            event_.thread_id = util::parse_uint(value).value_or(event_.thread_id);
        } else if (key == "Tmp_table" && yes(value)) {
            event_.set(ExecutionFlag::TmpTable);
        } else if (key == "Tmp_table_on_disk" && yes(value)) {
            event_.set(ExecutionFlag::TmpTableOnDisk);
        }
        return;
    case 'S':
        if (key == "Schema") {
            event_schema_ = unquote_name(value);
        } else if ((key == "Sort_scan_count" || key == "Sort_range_count") && positive(value)) {
            event_.set(ExecutionFlag::Filesort);
        }
        return;
    case 'F':
        if (key == "Full_scan" && yes(value)) {
            event_.set(ExecutionFlag::FullScan);
        } else if (key == "Filesort" && yes(value)) {
            event_.set(ExecutionFlag::Filesort);
        }
        return;
    case 'C':
        if (key == "Created_tmp_tables" && positive(value)) {
            event_.set(ExecutionFlag::TmpTable);
        } else if (key == "Created_tmp_disk_tables" && positive(value)) {
            event_.set(ExecutionFlag::TmpTableOnDisk);
        }
        return;
    default: return;
    }
}

bool SlowLogParser::parse_pre_body(std::string_view line) {
    const std::string_view t = util::trim(line);
    if (!t.ends_with(';')) return false;
    if (util::istarts_with(t, "use ")) {
        database_ = unquote_name(t.substr(4, t.size() - 5));
        return true;
    }
    if (util::istarts_with(t, "set timestamp=")) {
        if (auto ts = util::parse_uint(t.substr(14, t.size() - 15))) {
            event_.timestamp = static_cast<std::int64_t>(*ts);
        }
        return true;
    }
    return false;
}

void SlowLogParser::feed(std::string_view line) {
    ++counters_.lines;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    const bool event_header = line.starts_with("# Time:") || line.starts_with("# User@Host:") ||
                              line.starts_with("# Query_time:");
    if (state_ == State::Body && event_header) emit();

    if (line.starts_with("# ") && state_ != State::Body) {
        const std::string_view rest = line.substr(2);
        if (rest.starts_with("Time:")) {
            start_event();
            if (auto t = parse_log_time(rest.substr(5))) event_.timestamp = *t;
        } else if (rest.starts_with("User@Host:")) {
            if (state_ != State::Header || !user_.empty()) start_event();
            parse_user_host(rest.substr(10));
        } else if (rest.starts_with("Query_time:")) {
            if (state_ != State::Header || event_.query_time_us != 0) start_event();
            parse_attributes(rest);
        } else if (rest.starts_with("administrator command:")) {
            if (state_ == State::Header) {
                admin_command_ = true;
                state_ = State::Body;
            }
        } else if (state_ == State::Header) {
            parse_attributes(rest);
        }
        return;
    }

    if (is_server_banner(line)) {
        if (state_ == State::Body) emit();
        state_ = State::Idle;
        return;
    }

    switch (state_) {
    case State::Idle: return;
    case State::Header:
        if (parse_pre_body(line)) return;
        state_ = State::Body;
        sql_.assign(line);
        return;
    case State::Body:
        sql_ += '\n';
        sql_ += line;
        return;
    }
}

SlowLogParser::Counters SlowLogParser::parse_text(std::string_view text, const EventHandler& handler,
                                                  std::string_view initial_database) {
    SlowLogParser parser(handler);
    if (!initial_database.empty()) parser.set_database(initial_database);
    for_each_line(text, [&parser](std::string_view line) { parser.feed(line); });
    parser.finish();
    return parser.counters();
}

}
