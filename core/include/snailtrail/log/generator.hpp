#pragma once

#include <cstdint>
#include <ostream>
#include <random>
#include <string>

namespace snailtrail::log {

struct GeneratorOptions {
    std::uint64_t events = 10'000;
    std::uint64_t seed = 42;
    std::int64_t start_time = 1705312800;
    bool extra_fields = true;
};

class Random {
public:
    explicit Random(std::uint64_t seed) : engine_(seed) {}

    std::uint64_t next() { return engine_(); }
    std::uint64_t below(std::uint64_t bound) { return bound == 0 ? 0 : engine_() % bound; }
    std::uint64_t between(std::uint64_t low, std::uint64_t high) { return low + below(high - low + 1); }
    double unit() { return static_cast<double>(engine_() >> 11) * 0x1.0p-53; }
    double normal();
    double lognormal(double median, double sigma);

private:
    std::mt19937_64 engine_;
};

class SlowLogGenerator {
public:
    explicit SlowLogGenerator(GeneratorOptions options = {});

    void write(std::ostream& out);
    std::string generate();

private:
    GeneratorOptions options_;
};

}
