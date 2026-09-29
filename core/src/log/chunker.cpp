#include "snailtrail/log/chunker.hpp"

#include <algorithm>

#include "snailtrail/util/strings.hpp"

namespace snailtrail::log {

namespace {

constexpr std::size_t npos = std::string_view::npos;

std::size_t line_start_before(std::string_view log, std::size_t newline) noexcept {
    if (newline == 0) return 0;
    const std::size_t previous = log.rfind('\n', newline - 1);
    return previous == npos ? 0 : previous + 1;
}

}

std::size_t find_event_start(std::string_view log, std::size_t from) noexcept {
    if (from == 0) return 0;
    std::size_t search = from - 1;
    while (true) {
        const std::size_t user_host = log.find("\n# User@Host:", search);
        if (user_host == npos) {
            const std::size_t time = log.find("\n# Time:", from - 1);
            return time == npos ? npos : time + 1;
        }
        const std::size_t previous = line_start_before(log, user_host);
        const std::size_t start =
            log.substr(previous).starts_with("# Time:") ? previous : user_host + 1;
        if (start >= from) return start;
        search = user_host + 1;
    }
}

std::string_view last_use_database(std::string_view text) noexcept {
    std::size_t pos = text.size();
    while (pos > 0) {
        const std::size_t p = text.rfind("use ", pos - 1);
        if (p == npos) return {};
        if (p == 0 || text[p - 1] == '\n') {
            const std::size_t end = text.find('\n', p);
            std::string_view line = util::trim(text.substr(p, end == npos ? npos : end - p));
            if (line.ends_with(';')) {
                std::string_view name = util::trim(line.substr(4, line.size() - 5));
                if (name.size() >= 2 && name.front() == '`' && name.back() == '`') {
                    name = name.substr(1, name.size() - 2);
                }
                if (!name.empty()) return name;
            }
        }
        if (p == 0) return {};
        pos = p;
    }
    return {};
}

std::vector<LogChunk> split_log(std::string_view log, std::size_t parts,
                                std::size_t min_chunk_bytes) {
    std::vector<LogChunk> chunks;
    const std::size_t floor_bytes = std::max<std::size_t>(min_chunk_bytes, 1);
    parts = std::min(parts, std::max<std::size_t>(1, log.size() / floor_bytes));
    if (parts <= 1) {
        chunks.push_back({log, {}});
        return chunks;
    }

    std::vector<std::size_t> starts{0};
    for (std::size_t i = 1; i < parts; ++i) {
        const std::size_t target = log.size() / parts * i;
        const std::size_t start = find_event_start(log, target);
        if (start != npos && start > starts.back() && start < log.size()) starts.push_back(start);
    }

    std::string database;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::size_t end = i + 1 < starts.size() ? starts[i + 1] : log.size();
        LogChunk chunk{log.substr(starts[i], end - starts[i]), database};
        const std::string_view last = last_use_database(chunk.text);
        if (!last.empty()) database = last;
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

}
