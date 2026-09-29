# snailtrail/advisor

Public headers of the `snailtrail::advisor` module. Implementation notes and the rule
catalog: [`core/src/advisor`](../../../src/advisor).

| Header | Declares |
|---|---|
| `finding.hpp` | `Finding`, `Severity`, `severity_name()`, `parse_severity()` |
| `query_facts.hpp` | `QueryFacts`, `Predicate`, `ColumnUse`, `TableUse`, `OrGroup`, `collect_facts()` |
| `rule.hpp` | `Rule` (abstract), `RuleInfo`, `RuleContext` |
| `rules.hpp` | the fifteen concrete rules, `make_default_rules()` |
| `index_advisor.hpp` | `IndexCandidate`, `suggest_indexes()`, `usable_for_index()`, `index_name()` |
| `rule_engine.hpp` | `RuleEngine`: registry, enable/disable, `advise()` |
