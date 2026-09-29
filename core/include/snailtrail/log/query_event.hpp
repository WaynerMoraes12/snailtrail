#pragma once

#include <cstdint>
#include <string_view>

namespace snailtrail::log {

enum class ExecutionFlag : std::uint8_t {
    FullScan = 1U << 0U,
    Filesort = 1U << 1U,
    TmpTable = 1U << 2U,
    TmpTableOnDisk = 1U << 3U,
};

struct QueryEvent {
    std::string_view sql;
    std::string_view database;
    std::string_view user;
    std::string_view host;
    std::uint64_t query_time_us = 0;
    std::uint64_t lock_time_us = 0;
    std::uint64_t rows_sent = 0;
    std::uint64_t rows_examined = 0;
    std::uint64_t rows_affected = 0;
    std::uint64_t bytes_sent = 0;
    std::uint64_t thread_id = 0;
    std::int64_t timestamp = 0;
    std::uint8_t flags = 0;

    [[nodiscard]] bool has(ExecutionFlag flag) const noexcept {
        return (flags & static_cast<std::uint8_t>(flag)) != 0;
    }
    void set(ExecutionFlag flag) noexcept { flags |= static_cast<std::uint8_t>(flag); }
};

}
