#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace snailtrail::log {

struct LogChunk {
    std::string_view text;
    std::string initial_database;
};

std::size_t find_event_start(std::string_view log, std::size_t from) noexcept;

std::string_view last_use_database(std::string_view text) noexcept;

std::vector<LogChunk> split_log(std::string_view log, std::size_t parts,
                                std::size_t min_chunk_bytes = 256 * 1024);

}
