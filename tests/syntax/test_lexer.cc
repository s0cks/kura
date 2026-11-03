#include <gtest/gtest.h>

#include "frontend/syntax/lexer.h"
#include "test_util.h"

namespace kura::syntax::test {
using K = Token;
using Kinds_t = std::vector<Token::Kind>;

TEST(LexerTest, KebabCaseNamesAreOneToken) {
  EXPECT_EQ(Kinds("a-b"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("on-click-handler"), (Kinds_t{K::kIdent, K::kEof}));
  // a digit or an uppercase letter continues the name too
  EXPECT_EQ(Kinds("x-1"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("x-Y"), (Kinds_t{K::kIdent, K::kEof}));
}

TEST(LexerTest, BinaryMinusNeedsSpaceOnBothSides) {
  EXPECT_EQ(Kinds("a - b"), (Kinds_t{K::kIdent, K::kBinMinus, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("a\n- b"), (Kinds_t{K::kIdent, K::kBinMinus, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("a -b"), (Kinds_t{K::kIdent, K::kMinus, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("1-2"), (Kinds_t{K::kNumber, K::kMinus, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("a- b"), (Kinds_t{K::kIdent, K::kMinus, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("-1"), (Kinds_t{K::kMinus, K::kNumber, K::kEof}));
  // at the start of a file there is nothing in front of the `-`
  EXPECT_EQ(Kinds("- b"), (Kinds_t{K::kMinus, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("a--b"), (Kinds_t{K::kIdent, K::kMinus, K::kMinus, K::kIdent, K::kEof}));
}

TEST(LexerTest, KeywordsNamesAndColors) {
  EXPECT_EQ(Kinds("let"), (Kinds_t{K::kLet, K::kEof}));
  EXPECT_EQ(Kinds("let-x"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("letter"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("_"), (Kinds_t{K::kUnderscore, K::kEof}));
  EXPECT_EQ(Kinds("_x"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("None"), (Kinds_t{K::kNone, K::kEof}));
  EXPECT_EQ(Kinds("Nonexistent"), (Kinds_t{K::kUpperIdent, K::kEof}));
  EXPECT_EQ(Kinds("red"), (Kinds_t{K::kColorLit, K::kEof}));
  EXPECT_EQ(Kinds("red-ish"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("true false"), (Kinds_t{K::kTrue, K::kFalse, K::kEof}));
  // uppercase names don't take hyphens
  EXPECT_EQ(Kinds("Foo-bar"), (Kinds_t{K::kUpperIdent, K::kMinus, K::kIdent, K::kEof}));
}

TEST(LexerTest, NumbersAndUnits) {
  EXPECT_EQ(Kinds("10"), (Kinds_t{K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("10.5"), (Kinds_t{K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("10px"), (Kinds_t{K::kPxLit, K::kEof}));
  EXPECT_EQ(Kinds("1.5px"), (Kinds_t{K::kPxLit, K::kEof}));
  EXPECT_EQ(Kinds("50%"), (Kinds_t{K::kPctLit, K::kEof}));
  EXPECT_EQ(Kinds("2fr"), (Kinds_t{K::kFrLit, K::kEof}));
  EXPECT_EQ(Kinds("10."), (Kinds_t{K::kNumber, K::kDot, K::kEof}));
  EXPECT_EQ(Kinds("10.x"), (Kinds_t{K::kNumber, K::kDot, K::kIdent, K::kEof}));
}

TEST(LexerTest, Punctuation) {
  EXPECT_EQ(Kinds("|> || | ... . => == = != ! <= >= < >"),
            (Kinds_t{K::kPipe, K::kOr, K::kBar, K::kSpread, K::kDot, K::kFatArrow, K::kEq, K::kAssign, K::kNeq, K::kBang,
                     K::kLe, K::kGe, K::kLt, K::kGt, K::kEof}));
  // `>>` is two tokens so that Seq<Seq<Px>> closes both lists
  EXPECT_EQ(Kinds(">>"), (Kinds_t{K::kGt, K::kGt, K::kEof}));
}

TEST(LexerTest, StringsAndEscapes) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "\"a\\\"b\\n\"");
  const auto result = Lex(source, diagnostics);
  ASSERT_EQ(result.tokens.size(), 2U);
  EXPECT_EQ(result.tokens[0].kind, K::kString);
  EXPECT_TRUE(diagnostics.empty());
}

TEST(LexerTest, UnterminatedStringIsReportedAndStillAToken) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "let s = \"abc\nlet t = 1");
  const auto result = Lex(source, diagnostics);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.at(0).code, DiagnosticCode::kUnterminatedString);
  EXPECT_EQ(diagnostics.at(0).span, Span(8, 9));
  // lexing carried on into the second line
  EXPECT_EQ(result.tokens[3].kind, K::kString);
  EXPECT_EQ(result.tokens[4].kind, K::kLet);
}

TEST(LexerTest, UnknownEscapeIsReported) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "\"a\\qb\"");
  Lex(source, diagnostics);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.at(0).code, DiagnosticCode::kInvalidEscape);
  EXPECT_EQ(diagnostics.at(0).span, Span(2, 4));
}

TEST(LexerTest, DirectivesAndRawNames) {
  EXPECT_EQ(Kinds("@pure @no-inline"), (Kinds_t{K::kDirective, K::kDirective, K::kEof}));
  EXPECT_EQ(Kinds("`fn`"), (Kinds_t{K::kRawIdent, K::kEof}));
  DiagnosticList diagnostics{};
  const SourceFile source("t", "@ 1");
  const auto result = Lex(source, diagnostics);
  EXPECT_EQ(result.tokens[0].kind, K::kInvalid);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.at(0).code, DiagnosticCode::kBadDirective);
}

TEST(LexerTest, CommentsAreKeptAside) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "a // one\nb // two");
  const auto result = Lex(source, diagnostics);
  EXPECT_EQ(result.tokens.size(), 3U);  // a, b, eof
  ASSERT_EQ(result.comments.size(), 2U);
  EXPECT_EQ(source.GetText(result.comments[0]), "// one");
  EXPECT_EQ(source.GetText(result.comments[1]), "// two");
}

TEST(LexerTest, UnknownCharactersBecomeOneInvalidToken) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "a $ b \xC3\xA9 c");
  const auto result = Lex(source, diagnostics);
  EXPECT_EQ(diagnostics.size(), 2U);
  size_t invalid = 0;
  for (const auto& token : result.tokens)
    invalid += token.kind == K::kInvalid;
  EXPECT_EQ(invalid, 2U);
  // the two byte UTF-8 character is a single token
  EXPECT_EQ(source.GetText(result.tokens[3].span), "\xC3\xA9");
}

TEST(LexerTest, TokensCoverTheirTextAndEndWithOneEof) {
  const std::string text = "fn f(x: Int): Int => x + 10px // c\n";
  DiagnosticList diagnostics{};
  const SourceFile source("t", text);
  const auto result = Lex(source, diagnostics);
  ASSERT_FALSE(result.tokens.empty());
  EXPECT_EQ(result.tokens.back().kind, K::kEof);
  EXPECT_EQ(result.tokens.back().span, Span(text.size(), text.size()));
  uint32_t last_end = 0;
  for (size_t idx = 0; idx + 1 < result.tokens.size(); idx++) {
    const auto& token = result.tokens[idx];
    EXPECT_NE(token.kind, K::kEof);
    EXPECT_GE(token.span.begin, last_end);
    EXPECT_GT(token.span.length(), 0U);
    last_end = token.span.end;
  }
}
}  // namespace kura::syntax::test
