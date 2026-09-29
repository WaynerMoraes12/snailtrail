#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "snailtrail/log/query_event.hpp"

namespace snailtrail::log {

std::optional<std::uint64_t> parse_microseconds(std::string_view seconds) noexcept;
std::optional<std::int64_t> parse_log_time(std::string_view text) noexcept;

template <typename F>
void for_each_line(std::string_view text, F&& f) {
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            f(text.substr(start));
            return;
        }
        f(text.substr(start, end - start));
        start = end + 1;
    }
}

class SlowLogParser {
public:
    using EventHandler = std::function<void(const QueryEvent&)>;

    struct Counters {
        std::uint64_t lines = 0;
        std::uint64_t events = 0;
        std::uint64_t skipped = 0;
    };

    explicit SlowLogParser(EventHandler handler);

    void feed(std::string_view line);
    void finish();

    void set_database(std::string_view database);
    [[nodiscard]] std::string_view database() const noexcept { return database_; }
    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }

    static Counters parse_text(std::string_view text, const EventHandler& handler,
                               std::string_view initial_database = {});

private:
    enum class State : std::uint8_t { Idle, Header, Body };

    void start_event();
    void emit();
    void parse_user_host(std::string_view rest);
    void parse_attributes(std::string_view rest);
    void apply_attribute(std::string_view key, std::string_view value);
    bool parse_pre_body(std::string_view line);
    [[nodiscard]] static bool is_server_banner(std::string_view line) noexcept;

    EventHandler handler_;
    State state_ = State::Idle;
    QueryEvent event_;
    std::string sql_;
    std::string database_;
    std::string event_schema_;
    std::string user_;
    std::string host_;
    bool admin_command_ = false;
    Counters counters_;
};

}
