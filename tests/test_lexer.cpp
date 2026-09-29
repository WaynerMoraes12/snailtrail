#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "snailtrail/sql/lexer.hpp"

using namespace snailtrail::sql;

namespace {

std::vector<TokenKind> kinds(std::string_view sql) {
    std::vector<TokenKind> out;
    for (const Token& t : Lexer::tokenize(sql)) out.push_back(t.kind);
    return out;
}

std::vector<std::string> texts(std::string_view sql) {
    std::vector<std::string> out;
    for (const Token& t : Lexer::tokenize(sql)) {
        if (t.kind != TokenKind::End) out.emplace_back(t.text);
    }
    return out;
}

}

TEST(Lexer, SplitsASimpleSelect) {
    EXPECT_EQ(texts("SELECT a, b FROM t WHERE id = 42;"),
              (std::vector<std::string>{"SELECT", "a", ",", "b", "FROM", "t", "WHERE", "id", "=",
                                        "42", ";"}));
    EXPECT_EQ(kinds("f(x)"), (std::vector<TokenKind>{TokenKind::Word, TokenKind::LParen,
                                                     TokenKind::Word, TokenKind::RParen,
                                                     TokenKind::End}));
}

TEST(Lexer, EndsWithASingleEndToken) {
    const auto tokens = Lexer::tokenize("");
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens[0].kind, TokenKind::End);
    Lexer lexer("x");
    EXPECT_EQ(lexer.next().kind, TokenKind::Word);
    EXPECT_EQ(lexer.next().kind, TokenKind::End);
    EXPECT_EQ(lexer.next().kind, TokenKind::End);
}

TEST(Lexer, ReadsStringsWithEscapesAndDoubledQuotes) {
    const auto tokens = Lexer::tokenize(R"('it''s' "say \"hi\"" 'a\'b')");
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0].text, "'it''s'");
    EXPECT_EQ(tokens[1].text, R"("say \"hi\"")");
    EXPECT_EQ(tokens[2].text, R"('a\'b')");
    EXPECT_EQ(unquote_string(tokens[0].text), "it's");
    EXPECT_EQ(unquote_string(tokens[1].text), "say \"hi\"");
    EXPECT_EQ(unquote_string(tokens[2].text), "a'b");
}

TEST(Lexer, KeepsLikeEscapesTheWayMySqlDoes) {
    EXPECT_EQ(unquote_string(R"('50\%')"), R"(50\%)");
    EXPECT_EQ(unquote_string(R"('a\nb')"), "a\nb");
}

TEST(Lexer, UnterminatedStringRunsToTheEnd) {
    const auto tokens = Lexer::tokenize("SELECT 'oops");
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens[1].kind, TokenKind::String);
    EXPECT_EQ(tokens[1].text, "'oops");
}

TEST(Lexer, ReadsPrefixedLiterals) {
    const auto tokens = Lexer::tokenize("N'nat' _utf8mb4'x' X'1F' b'0101' 0x1F 0b11");
    ASSERT_EQ(tokens.size(), 7U);
    EXPECT_EQ(tokens[0].kind, TokenKind::String);
    EXPECT_EQ(tokens[1].kind, TokenKind::String);
    EXPECT_EQ(tokens[1].text, "_utf8mb4'x'");
    EXPECT_EQ(tokens[2].kind, TokenKind::HexNumber);
    EXPECT_EQ(tokens[3].kind, TokenKind::HexNumber);
    EXPECT_EQ(tokens[4].kind, TokenKind::HexNumber);
    EXPECT_EQ(tokens[5].kind, TokenKind::HexNumber);
}

TEST(Lexer, ReadsNumbers) {
    EXPECT_EQ(texts("1 3.14 .5 1e10 2.5E-3 7."),
              (std::vector<std::string>{"1", "3.14", ".5", "1e10", "2.5E-3", "7."}));
    for (TokenKind k : kinds("1 3.14 .5 1e10 2.5E-3")) {
        if (k != TokenKind::End) {
            EXPECT_EQ(k, TokenKind::Number);
        }
    }
}

TEST(Lexer, IdentifiersMayStartWithADigit) {
    const auto tokens = Lexer::tokenize("SELECT 1st_place, 2fa FROM t");
    EXPECT_EQ(tokens[1].kind, TokenKind::Word);
    EXPECT_EQ(tokens[1].text, "1st_place");
    EXPECT_EQ(tokens[3].kind, TokenKind::Word);
}

TEST(Lexer, DotAfterAnIdentifierIsQualification) {
    EXPECT_EQ(kinds("t.5"), (std::vector<TokenKind>{TokenKind::Word, TokenKind::Dot,
                                                    TokenKind::Number, TokenKind::End}));
    EXPECT_EQ(texts("o.customer_id"), (std::vector<std::string>{"o", ".", "customer_id"}));
}

TEST(Lexer, ReadsBacktickIdentifiers) {
    const auto tokens = Lexer::tokenize("SELECT `order`, `we``ird` FROM `my table`");
    EXPECT_EQ(tokens[1].kind, TokenKind::QuotedIdentifier);
    EXPECT_EQ(unquote_identifier(tokens[1]), "order");
    EXPECT_EQ(unquote_identifier(tokens[3]), "we`ird");
    EXPECT_EQ(unquote_identifier(tokens[5]), "my table");
}

TEST(Lexer, ReadsVariablesAndPlaceholders) {
    const auto tokens = Lexer::tokenize("@x @'quoted name' @@session.sql_mode ?");
    ASSERT_EQ(tokens.size(), 5U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Variable);
    EXPECT_EQ(tokens[1].text, "@'quoted name'");
    EXPECT_EQ(tokens[2].text, "@@session.sql_mode");
    EXPECT_EQ(tokens[3].kind, TokenKind::Placeholder);
}

TEST(Lexer, PrefersTheLongestOperator) {
    EXPECT_EQ(texts("a<=>b c->>'$.x' d<>e f!=g h:=1 i<<2"),
              (std::vector<std::string>{"a", "<=>", "b", "c", "->>", "'$.x'", "d", "<>", "e",
                                        "f", "!=", "g", "h", ":=", "1", "i", "<<", "2"}));
}

TEST(Lexer, SkipsComments) {
    EXPECT_EQ(texts("SELECT 1 -- trailing\n, 2 # hash\n, /* block */ 3 /*+ BKA(t) */"),
              (std::vector<std::string>{"SELECT", "1", ",", "2", ",", "3"}));
}

TEST(Lexer, DoubleDashWithoutSpaceIsArithmetic) {
    EXPECT_EQ(texts("1--1"), (std::vector<std::string>{"1", "-", "-", "1"}));
}

TEST(Lexer, LexesExecutableComments) {
    EXPECT_EQ(texts("SELECT /*!40001 SQL_NO_CACHE */ * FROM t"),
              (std::vector<std::string>{"SELECT", "SQL_NO_CACHE", "*", "FROM", "t"}));
    EXPECT_EQ(texts("/*!40101 SET @a=@@b */;"),
              (std::vector<std::string>{"SET", "@a", "=", "@@b", ";"}));
}

TEST(Lexer, RecordsOffsets) {
    const auto tokens = Lexer::tokenize("SELECT  x");
    EXPECT_EQ(tokens[0].offset, 0U);
    EXPECT_EQ(tokens[1].offset, 8U);
    EXPECT_EQ(tokens[2].offset, 9U);
}

TEST(Lexer, NeverThrowsOnGarbage) {
    const std::string garbage = "\x01\x02 SELECT \xff\xfe 'x \\";
    EXPECT_NO_THROW(Lexer::tokenize(garbage));
    EXPECT_EQ(Lexer::tokenize(garbage).back().kind, TokenKind::End);
}
