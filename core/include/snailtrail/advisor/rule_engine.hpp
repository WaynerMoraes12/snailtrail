#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/advisor/finding.hpp"
#include "snailtrail/advisor/rule.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/stats/query_class.hpp"

namespace snailtrail::advisor {

class RuleEngine {
public:
    RuleEngine() = default;
    RuleEngine(RuleEngine&&) noexcept = default;
    RuleEngine& operator=(RuleEngine&&) noexcept = default;
    RuleEngine(const RuleEngine&) = delete;
    RuleEngine& operator=(const RuleEngine&) = delete;
    ~RuleEngine() = default;

    static RuleEngine with_default_rules();

    RuleEngine& add(std::unique_ptr<Rule> rule);
    bool disable(std::string_view id_or_name);

    [[nodiscard]] bool is_enabled(std::string_view id_or_name) const;
    [[nodiscard]] const Rule* find(std::string_view id_or_name) const;
    [[nodiscard]] std::vector<const Rule*> rules() const;

    [[nodiscard]] std::vector<Finding> advise(std::string_view sql,
                                              const schema::SchemaCatalog* catalog = nullptr,
                                              const stats::QueryClass* stats = nullptr) const;

private:
    std::vector<std::unique_ptr<Rule>> rules_;
    std::vector<std::string> disabled_;
};

}
