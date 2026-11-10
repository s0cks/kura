#include <gtest/gtest.h>

#include "frontend/syntax/lexer.h"
#include "test_util.h"

namespace kura::syntax::test {
using K = Token;
using Kinds_t = std::vector<Token::Kind>;

TEST(LexerTest, KebabCaseNamesAreOneToken) {
  EXPECT_EQ(Kinds("a-b"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("on-click-handler"), (Kinds_t{K::kIdent, K::kEof}));
  // an uppercase letter continues the name too, and so does a digit when a letter comes with it
  EXPECT_EQ(Kinds("x-Y"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("pos-2d-to-3d"), (Kinds_t{K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("x-1a"), (Kinds_t{K::kIdent, K::kEof}));
}

TEST(LexerTest, ADashBeforeOnlyDigitsIsASubtraction) {
  // `x-1` is `x` minus `1`, not one name; the parser then asks for spaces around the `-`
  EXPECT_EQ(Kinds("x-1"), (Kinds_t{K::kIdent, K::kMinus, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("count-20"), (Kinds_t{K::kIdent, K::kMinus, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("a-b-1"), (Kinds_t{K::kIdent, K::kMinus, K::kNumber, K::kEof}));
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

TEST(LexerTest, RangesCoalesceOptionalChainingAndCompoundAssignment) {
  EXPECT_EQ(Kinds(".. ..= ... ?? ?. <| += -= *= /= %="),
            (Kinds_t{K::kDotDot, K::kDotDotEq, K::kSpread, K::kQQ, K::kQDot, K::kPipeBack, K::kPlusEq, K::kMinusEq,
                     K::kStarEq, K::kSlashEq, K::kPercentEq, K::kEof}));
  // `0..10` is a range, not the start of a decimal number
  EXPECT_EQ(Kinds("0..10"), (Kinds_t{K::kNumber, K::kDotDot, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("0..=10"), (Kinds_t{K::kNumber, K::kDotDotEq, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("0.5..1.5"), (Kinds_t{K::kNumber, K::kDotDot, K::kNumber, K::kEof}));
  // an optional field is still `name?:`
  EXPECT_EQ(Kinds("z?: Float"), (Kinds_t{K::kIdent, K::kQMark, K::kColon, K::kUpperIdent, K::kEof}));
  EXPECT_EQ(Kinds("a?.b ?? c"), (Kinds_t{K::kIdent, K::kQDot, K::kIdent, K::kQQ, K::kIdent, K::kEof}));
  // `<|` and `|>` don't get in the way of unions and comparisons
  EXPECT_EQ(Kinds("a <= b"), (Kinds_t{K::kIdent, K::kLe, K::kIdent, K::kEof}));
  EXPECT_EQ(Kinds("x -= 1"), (Kinds_t{K::kIdent, K::kMinusEq, K::kNumber, K::kEof}));
  EXPECT_EQ(Kinds("x == -1"), (Kinds_t{K::kIdent, K::kEq, K::kMinus, K::kNumber, K::kEof}));
  // `>>` and `<<` stay two tokens each: the parser joins them where an operator is expected
  EXPECT_EQ(Kinds("<<"), (Kinds_t{K::kLt, K::kLt, K::kEof}));
}

TEST(LexerTest, NewKeywords) {
  EXPECT_EQ(Kinds("when is static-assert"), (Kinds_t{K::kWhen, K::kIs, K::kStaticAssert, K::kEof}));
  // only the whole word is the keyword
  EXPECT_EQ(Kinds("is-large set-x when-ready static-assertion"),
            (Kinds_t{K::kIdent, K::kIdent, K::kIdent, K::kIdent, K::kEof}));
}

TEST(LexerTest, InterpolatedStringsAreOneToken) {
  const auto count = [](const std::string& text) {
    DiagnosticList diagnostics{};
    const SourceFile source("t", text);
    const auto result = Lex(source, diagnostics);
    EXPECT_TRUE(diagnostics.empty()) << text;
    return result.tokens.size();
  };
  EXPECT_EQ(count("\"a {b} c\""), 2U);
  EXPECT_EQ(count("\"{pos.x}, {pos.y}\""), 2U);
  // a string inside an interpolation, with its own braces and quotes
  EXPECT_EQ(count("\"a {f(\"x}\")} b\""), 2U);
  EXPECT_EQ(count("\"a {g(\"{h}\")} b\" 1"), 3U);
  // escaped braces
  EXPECT_EQ(count("\"\\{ not an interpolation \\}\""), 2U);
  // a lone `}` is plain text
  EXPECT_EQ(count("\"a } b\""), 2U);
}

TEST(LexerTest, AnUnclosedInterpolationIsReportedOnce) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "let s = \"a {b c\nlet t = 1");
  const auto result = Lex(source, diagnostics);
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.at(0).code, DiagnosticCode::kBadInterpolation);
  EXPECT_EQ(diagnostics.at(0).span, Span(11, 12));
  EXPECT_EQ(result.tokens[4].kind, K::kLet);
}

TEST(LexerTest, LexRangeKeepsOffsetsInTheWholeFile) {
  DiagnosticList diagnostics{};
  const SourceFile source("t", "xx { a + b } yy");
  const auto result = LexRange(source, 4, 11, diagnostics);
  ASSERT_EQ(result.tokens.size(), 4U);
  EXPECT_EQ(result.tokens[0].span, Span(5, 6));
  EXPECT_EQ(result.tokens[1].kind, K::kPlus);
  EXPECT_EQ(result.tokens[2].span, Span(9, 10));
  EXPECT_EQ(result.tokens.back().kind, K::kEof);
}
}  // namespace kura::syntax::test
