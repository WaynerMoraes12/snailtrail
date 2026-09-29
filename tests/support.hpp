#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace snailtrail::testing {

inline std::string sample_path(const std::string& name) {
    const char* override_dir = std::getenv("SNAILTRAIL_SAMPLES_DIR");
    const std::string dir = override_dir != nullptr ? override_dir : SNAILTRAIL_SAMPLES_DIR;
    return dir + "/" + name;
}

inline std::string read_sample(const std::string& name) {
    std::ifstream in(sample_path(name), std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}
