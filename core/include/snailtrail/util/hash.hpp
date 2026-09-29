#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snailtrail::util {

class Fnv1a64 {
public:
    static constexpr std::uint64_t offset_basis = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t prime = 0x100000001b3ULL;

    constexpr void update(char c) noexcept {
        hash_ ^= static_cast<unsigned char>(c);
        hash_ *= prime;
    }

    constexpr void update(std::string_view s) noexcept {
        for (char c : s) update(c);
    }

    [[nodiscard]] constexpr std::uint64_t digest() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = offset_basis;
};

constexpr std::uint64_t fnv1a_64(std::string_view s) noexcept {
    Fnv1a64 h;
    h.update(s);
    return h.digest();
}

std::string to_hex(std::uint64_t value);
bool from_hex(std::string_view text, std::uint64_t& value) noexcept;

}
