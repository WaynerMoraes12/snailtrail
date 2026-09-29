add_library(snailtrail_core STATIC
    src/util/strings.cpp
    src/util/hash.cpp
    src/util/json_writer.cpp
    src/sql/token.cpp
    src/sql/lexer.cpp
    src/sql/keywords.cpp
    src/sql/statement_kind.cpp
    src/sql/fingerprint.cpp
    src/sql/ast.cpp
    src/sql/parser.cpp
    src/sql/sql_writer.cpp
    src/sql/ast_printer.cpp
    src/schema/catalog.cpp
    src/log/slow_log_parser.cpp
    src/log/mapped_file.cpp
    src/log/chunker.cpp
    src/log/generator.cpp
    src/stats/histogram.cpp
    src/stats/query_class.cpp
    src/stats/aggregator.cpp
    src/advisor/finding.cpp
    src/advisor/query_facts.cpp
    src/advisor/helpers.cpp
    src/advisor/rule.cpp
    src/advisor/index_advisor.cpp
    src/advisor/rules_structure.cpp
    src/advisor/rules_predicates.cpp
    src/advisor/rules_metrics.cpp
    src/advisor/rule_engine.cpp
)
add_library(snailtrail::core ALIAS snailtrail_core)

target_include_directories(snailtrail_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(snailtrail_core PUBLIC cxx_std_20)
target_compile_definitions(snailtrail_core PUBLIC
    SNAILTRAIL_VERSION="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

set_target_properties(snailtrail_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(snailtrail_core PUBLIC Threads::Threads)

snailtrail_set_warnings(snailtrail_core)
