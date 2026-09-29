#pragma once

#include <memory>
#include <vector>

#include "snailtrail/advisor/rule.hpp"

namespace snailtrail::advisor {

class MissingIndexRule final : public Rule {
public:
    MissingIndexRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class UnboundedWriteRule final : public Rule {
public:
    UnboundedWriteRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class CartesianJoinRule final : public Rule {
public:
    CartesianJoinRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class NonSargablePredicateRule final : public Rule {
public:
    NonSargablePredicateRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class ImplicitConversionRule final : public Rule {
public:
    ImplicitConversionRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class LeadingWildcardRule final : public Rule {
public:
    LeadingWildcardRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class NotInSubqueryRule final : public Rule {
public:
    NotInSubqueryRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class DeepPaginationRule final : public Rule {
public:
    DeepPaginationRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class OrderByRandRule final : public Rule {
public:
    OrderByRandRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class OrAcrossColumnsRule final : public Rule {
public:
    OrAcrossColumnsRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class LargeInListRule final : public Rule {
public:
    LargeInListRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class HavingWithoutAggregateRule final : public Rule {
public:
    HavingWithoutAggregateRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class SelectStarRule final : public Rule {
public:
    SelectStarRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class RowsExaminedRatioRule final : public Rule {
public:
    RowsExaminedRatioRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

class TempTablesOnDiskRule final : public Rule {
public:
    TempTablesOnDiskRule();

private:
    void check(const RuleContext& context, std::vector<Finding>& out) const override;
};

std::vector<std::unique_ptr<Rule>> make_default_rules();

}
