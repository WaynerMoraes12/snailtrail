#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/sql/statement_kind.hpp"
#include "snailtrail/sql/token.hpp"

namespace snailtrail::sql {

struct Fingerprint {
    std::string text;
    std::uint64_t id = 0;
    StatementKind kind = StatementKind::Unknown;

    [[nodiscard]] std::string id_hex() const;
};

class Fingerprinter {
public:
    Fingerprint compute(std::string_view sql);
    void compute_into(std::string_view sql, Fingerprint& out);

private:
    std::vector<Token> tokens_;
};

Fingerprint fingerprint(std::string_view sql);

}
