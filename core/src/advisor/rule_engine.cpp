#include "snailtrail/advisor/rule_engine.hpp"

#include <algorithm>

#include "snailtrail/advisor/query_facts.hpp"
#include "snailtrail/advisor/rules.hpp"
#include "snailtrail/sql/fingerprint.hpp"
#include "snailtrail/sql/parser.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::advisor {

std::vector<std::unique_ptr<Rule>> make_default_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::make_unique<MissingIndexRule>());
    rules.push_back(std::make_unique<UnboundedWriteRule>());
    rules.push_back(std::make_unique<CartesianJoinRule>());
    rules.push_back(std::make_unique<NonSargablePredicateRule>());
    rules.push_back(std::make_unique<ImplicitConversionRule>());
    rules.push_back(std::make_unique<LeadingWildcardRule>());
    rules.push_back(std::make_unique<NotInSubqueryRule>());
    rules.push_back(std::make_unique<DeepPaginationRule>());
    rules.push_back(std::make_unique<OrderByRandRule>());
    rules.push_back(std::make_unique<OrAcrossColumnsRule>());
    rules.push_back(std::make_unique<LargeInListRule>());
    rules.push_back(std::make_unique<HavingWithoutAggregateRule>());
    rules.push_back(std::make_unique<SelectStarRule>());
    rules.push_back(std::make_unique<RowsExaminedRatioRule>());
    rules.push_back(std::make_unique<TempTablesOnDiskRule>());
    return rules;
}

RuleEngine RuleEngine::with_default_rules() {
    RuleEngine engine;
    for (auto& rule : make_default_rules()) engine.add(std::move(rule));
    return engine;
}

RuleEngine& RuleEngine::add(std::unique_ptr<Rule> rule) {
    rules_.push_back(std::move(rule));
    return *this;
}

const Rule* RuleEngine::find(std::string_view id_or_name) const {
    for (const auto& rule : rules_) {
        if (util::iequals(rule->id(), id_or_name) || util::iequals(rule->name(), id_or_name)) {
            return rule.get();
        }
    }
    return nullptr;
}

bool RuleEngine::disable(std::string_view id_or_name) {
    if (util::iequals(id_or_name, "ST000") || util::iequals(id_or_name, "unparsed")) {
        disabled_.emplace_back("ST000");
        return true;
    }
    const Rule* rule = find(id_or_name);
    if (rule == nullptr) return false;
    disabled_.emplace_back(rule->id());
    return true;
}

bool RuleEngine::is_enabled(std::string_view id_or_name) const {
    std::string id(id_or_name);
    if (const Rule* rule = find(id_or_name)) id = rule->id();
    return std::none_of(disabled_.begin(), disabled_.end(),
                        [&](const std::string& d) { return util::iequals(d, id); });
}

std::vector<const Rule*> RuleEngine::rules() const {
    std::vector<const Rule*> out;
    out.reserve(rules_.size());
    for (const auto& rule : rules_) out.push_back(rule.get());
    return out;
}

std::vector<Finding> RuleEngine::advise(std::string_view sql, const schema::SchemaCatalog* catalog,
                                        const stats::QueryClass* stats) const {
    std::string error;
    const sql::StatementPtr statement = sql::try_parse(sql, &error);
    if (!statement) return advise(sql, nullptr, nullptr, error, catalog, stats);
    const QueryFacts facts = collect_facts(*statement, catalog);
    return advise(sql, statement.get(), &facts, {}, catalog, stats);
}

std::vector<Finding> RuleEngine::advise(std::string_view sql, const sql::Statement* statement,
                                        const QueryFacts* facts, std::string_view parse_error,
                                        const schema::SchemaCatalog* catalog,
                                        const stats::QueryClass* stats) const {
    std::vector<Finding> out;
    const RuleContext context{sql, statement, facts, catalog, stats};
    if (statement == nullptr && sql::is_dml(sql::fingerprint(sql).kind) && is_enabled("ST000")) {
        out.push_back(Finding{"ST000", "unparsed", Severity::Info,
                              "Statement not fully understood: structural checks skipped",
                              std::string(parse_error),
                              "Metric-based checks still apply. Please report the statement if it is valid MySQL."});
    }

    for (const auto& rule : rules_) {
        if (is_enabled(rule->id())) rule->evaluate(context, out);
    }
    std::stable_sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return a.severity > b.severity;
        return a.rule_id < b.rule_id;
    });
    return out;
}

}
