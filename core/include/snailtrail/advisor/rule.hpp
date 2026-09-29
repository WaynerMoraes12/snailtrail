#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "snailtrail/advisor/finding.hpp"
#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/schema/catalog.hpp"
#include "snailtrail/sql/ast.hpp"
#include "snailtrail/stats/query_class.hpp"

namespace snailtrail::advisor {

struct RuleContext {
    std::string_view sql;
    const sql::Statement* statement = nullptr;
    const QueryFacts* facts = nullptr;
    const schema::SchemaCatalog* catalog = nullptr;
    const stats::QueryClass* stats = nullptr;
};

struct RuleInfo {
    std::string_view id;
    std::string_view name;
    std::string_view summary;
    Severity severity = Severity::Info;
    bool needs_statement = true;
    bool needs_schema = false;
    bool needs_stats = false;
};

class Rule {
public:
    explicit Rule(RuleInfo info) noexcept : info_(info) {}
    Rule(const Rule&) = delete;
    Rule& operator=(const Rule&) = delete;
    Rule(Rule&&) = delete;
    Rule& operator=(Rule&&) = delete;
    virtual ~Rule() = default;

    [[nodiscard]] const RuleInfo& info() const noexcept { return info_; }
    [[nodiscard]] std::string_view id() const noexcept { return info_.id; }
    [[nodiscard]] std::string_view name() const noexcept { return info_.name; }

    void evaluate(const RuleContext& context, std::vector<Finding>& out) const;

protected:
    [[nodiscard]] Finding finding(Severity severity, std::string title, std::string detail,
                                  std::string suggestion = {}) const;

private:
    virtual void check(const RuleContext& context, std::vector<Finding>& out) const = 0;

    RuleInfo info_;
};

}
