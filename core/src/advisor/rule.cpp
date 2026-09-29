#include "snailtrail/advisor/rule.hpp"

namespace snailtrail::advisor {

void Rule::evaluate(const RuleContext& context, std::vector<Finding>& out) const {
    if (info_.needs_statement && (context.statement == nullptr || context.facts == nullptr)) return;
    if (info_.needs_schema && (context.catalog == nullptr || context.catalog->empty())) return;
    if (info_.needs_stats && context.stats == nullptr) return;
    check(context, out);
}

Finding Rule::finding(Severity severity, std::string title, std::string detail,
                      std::string suggestion) const {
    return Finding{std::string(info_.id), std::string(info_.name), severity,
                   std::move(title),      std::move(detail),       std::move(suggestion)};
}

}
