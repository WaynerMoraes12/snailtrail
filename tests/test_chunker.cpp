#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "snailtrail/log/chunker.hpp"
#include "snailtrail/log/generator.hpp"
#include "snailtrail/log/mapped_file.hpp"
#include "snailtrail/log/slow_log_parser.hpp"

using namespace snailtrail::log;

namespace {

using EventKey = std::tuple<std::string, std::string, std::uint64_t, std::int64_t>;

std::vector<EventKey> collect(std::string_view text, std::string_view database = {}) {
    std::vector<EventKey> out;
    SlowLogParser::parse_text(
        text,
        [&](const QueryEvent& e) {
            out.emplace_back(std::string(e.sql), std::string(e.database), e.query_time_us,
                             e.timestamp);
        },
        database);
    return out;
}

std::string generated(std::uint64_t events, std::uint64_t seed = 7) {
    GeneratorOptions options;
    options.events = events;
    options.seed = seed;
    return SlowLogGenerator(options).generate();
}

class TempFile {
public:
    explicit TempFile(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() /
                ("snailtrail-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".log");
        std::ofstream out(path_, std::ios::binary);
        out << content;
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}

TEST(Chunker, FindsEventStartsIncludingTheTimeLine) {
    const std::string log = "# Time: t1\n# User@Host: a\n# Query_time: 1\nSELECT 1;\n"
                            "# Time: t2\n# User@Host: b\n# Query_time: 1\nSELECT 2;\n";
    const std::size_t second = log.find("# Time: t2");
    EXPECT_EQ(find_event_start(log, 0), 0U);
    EXPECT_EQ(find_event_start(log, 5), second);
    EXPECT_EQ(find_event_start(log, second), second);
    EXPECT_EQ(find_event_start(log, second + 3), std::string_view::npos);

    const std::string no_time = "# User@Host: a\nSELECT 1;\n# User@Host: b\nSELECT 2;\n";
    EXPECT_EQ(find_event_start(no_time, 3), no_time.find("# User@Host: b"));
}

TEST(Chunker, FindsTheLastUseStatement) {
    EXPECT_EQ(last_use_database("use a;\nSELECT 1;\nuse `b`;\nSELECT because;\n"), "b");
    EXPECT_EQ(last_use_database("SELECT 'nothing to use here';\n"), "");
    EXPECT_EQ(last_use_database("use only;"), "only");
    EXPECT_EQ(last_use_database(""), "");
}

TEST(Chunker, SmallLogsStayInOneChunk) {
    const std::string log = generated(50);
    const auto chunks = split_log(log, 8);
    ASSERT_EQ(chunks.size(), 1U);
    EXPECT_EQ(chunks[0].text.size(), log.size());
}

TEST(Chunker, ChunksConcatenateBackToTheLog) {
    const std::string log = generated(6000);
    const auto chunks = split_log(log, 8, 64 * 1024);
    ASSERT_GT(chunks.size(), 4U);
    std::string joined;
    for (const auto& c : chunks) joined += c.text;
    EXPECT_EQ(joined, log);
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        EXPECT_TRUE(chunks[i].text.starts_with("# Time:")) << i;
        EXPECT_EQ(chunks[i].initial_database, "shop") << i;
    }
}

TEST(Chunker, ParsingChunksYieldsExactlyTheSequentialEvents) {
    const std::string log = generated(6000, 99);
    const auto sequential = collect(log);
    std::vector<EventKey> chunked;
    for (const auto& chunk : split_log(log, 16, 32 * 1024)) {
        const auto part = collect(chunk.text, chunk.initial_database);
        chunked.insert(chunked.end(), part.begin(), part.end());
    }
    ASSERT_FALSE(sequential.empty());
    EXPECT_EQ(chunked, sequential);
}

TEST(MappedFile, MapsAFile) {
    const std::string content = generated(200);
    TempFile file(content);
    MappedFile mapped(file.path());
    EXPECT_EQ(mapped.size(), content.size());
    EXPECT_EQ(mapped.view(), content);

    MappedFile moved(std::move(mapped));
    EXPECT_EQ(moved.view(), content);
    EXPECT_EQ(mapped.size(), 0U);
}

TEST(MappedFile, HandlesEmptyAndMissingFiles) {
    TempFile empty("");
    MappedFile mapped(empty.path());
    EXPECT_EQ(mapped.size(), 0U);
    EXPECT_TRUE(mapped.view().empty());
    EXPECT_THROW(MappedFile("/definitely/not/here.log"), std::system_error);
}

TEST(Generator, IsDeterministicPerSeed) {
    EXPECT_EQ(generated(300, 1), generated(300, 1));
    EXPECT_NE(generated(300, 1), generated(300, 2));
}

TEST(Generator, WritesTheRequestedNumberOfEvents) {
    const std::string log = generated(2000);
    SlowLogParser::Counters counters =
        SlowLogParser::parse_text(log, [](const QueryEvent&) {});
    EXPECT_EQ(counters.events + counters.skipped, 2000U);
    EXPECT_GT(counters.skipped, 0U);
}

TEST(Generator, RandomHelpersStayInRange) {
    Random r(3);
    for (int i = 0; i < 1000; ++i) {
        const double u = r.unit();
        EXPECT_GE(u, 0.0);
        EXPECT_LT(u, 1.0);
        const auto b = r.between(5, 9);
        EXPECT_GE(b, 5U);
        EXPECT_LE(b, 9U);
        EXPECT_GT(r.lognormal(10.0, 0.5), 0.0);
    }
    EXPECT_EQ(r.below(0), 0U);
}
