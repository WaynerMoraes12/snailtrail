#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/schema/catalog.hpp"

namespace snailtrail::advisor {

enum class TailKind : std::uint8_t { None, Range, Sort, Group };

struct ExistingIndex {
    std::string name;
    std::vector<std::string> columns;
};

struct IndexCandidate {
    std::string table;
    std::vector<std::string> equality;
    std::vector<std::string> tail;
    TailKind tail_kind = TailKind::None;
    std::string name;
    std::string ddl;
    std::string partial_index;
    std::vector<ExistingIndex> redundant;
    bool schema_checked = false;

    [[nodiscard]] std::vector<std::string> columns() const;
};

bool usable_for_index(const Predicate& predicate, const schema::SchemaCatalog* catalog);

std::vector<IndexCandidate> suggest_indexes(const QueryFacts& facts,
                                            const schema::SchemaCatalog* catalog);

std::string index_name(const std::string& table, const std::vector<std::string>& columns);

}
