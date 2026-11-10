#ifndef KURA_SYNTAX_TOKEN_H
#define KURA_SYNTAX_TOKEN_H

#include <bitset>
#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <string_view>

#include "compiler/frontend/syntax/source.h"

namespace kura::syntax {
// Name, spelling. Fixed tokens spell themselves; the rest are described in
// angle brackets and rendered as "an identifier" and so on in diagnostics.
// clang-format off
#define FOR_EACH_KEYWORD_TOKEN(V) \
  V(Let,    "let")                \
  V(Fn,     "fn")                 \
  V(Type,   "type")               \
  V(If,     "if")                 \
  V(Else,   "else")               \
  V(Match,  "match")              \
  V(For,    "for")                \
  V(In,     "in")                 \
  V(Fold,   "fold")               \
  V(Use,    "use")                \
  V(As,     "as")                 \
  V(Extern, "extern")             \
  V(When,   "when")               \
  V(Is,     "is")                 \
  V(StaticAssert, "static-assert") \
  V(True,   "true")               \
  V(False,  "false")              \
  V(None,   "None")

#define FOR_EACH_PUNCTUATION_TOKEN(V) \
  V(BinMinus, "-")                    \
  V(Minus,    "-")                    \
  V(Plus,     "+")                    \
  V(Star,     "*")                    \
  V(Slash,    "/")                    \
  V(Percent,  "%")                    \
  V(Pipe,     "|>")                   \
  V(Or,       "||")                   \
  V(And,      "&&")                   \
  V(Eq,       "==")                   \
  V(Neq,      "!=")                   \
  V(Le,       "<=")                   \
  V(Ge,       ">=")                   \
  V(Lt,       "<")                    \
  V(Gt,       ">")                    \
  V(Bang,     "!")                    \
  V(FatArrow, "=>")                   \
  V(Assign,   "=")                    \
  V(Spread,   "...")                  \
  V(DotDotEq, "..=")                  \
  V(DotDot,   "..")                   \
  V(Dot,      ".")                    \
  V(QQ,       "??")                   \
  V(QDot,     "?.")                   \
  V(PipeBack, "<|")                   \
  V(PlusEq,   "+=")                   \
  V(MinusEq,  "-=")                   \
  V(StarEq,   "*=")                   \
  V(SlashEq,  "/=")                   \
  V(PercentEq, "%=")                  \
  V(Comma,    ",")                    \
  V(Colon,    ":")                    \
  V(QMark,    "?")                    \
  V(Hash,     "#")                    \
  V(Bar,      "|")                    \
  V(LParen,   "(")                    \
  V(RParen,   ")")                    \
  V(LBrace,   "{")                    \
  V(RBrace,   "}")                    \
  V(LBrack,   "[")                    \
  V(RBrack,   "]")

#define FOR_EACH_VALUE_TOKEN(V)     \
  V(PxLit,      "<px>")             \
  V(PctLit,     "<percent>")        \
  V(FrLit,      "<fr>")             \
  V(Number,     "<number>")         \
  V(String,     "<string>")         \
  V(ColorLit,   "<color>")          \
  V(Underscore, "_")                \
  V(RawIdent,   "<raw-name>")       \
  V(Ident,      "<name>")           \
  V(UpperIdent, "<Name>")           \
  V(Directive,  "<directive>")

#define FOR_EACH_TOKEN(V)           \
  FOR_EACH_KEYWORD_TOKEN(V)         \
  FOR_EACH_PUNCTUATION_TOKEN(V)     \
  FOR_EACH_VALUE_TOKEN(V)           \
  V(Invalid, "<invalid>")           \
  V(Eof,     "<end of file>")
// clang-format on

struct Token {
  enum Kind : uint8_t {
#define DEFINE_KIND(Name, Spelling) k##Name,
    FOR_EACH_TOKEN(DEFINE_KIND)
#undef DEFINE_KIND
        kTotalNumberOfTokens,
  };

  using KindSet = std::bitset<kTotalNumberOfTokens>;

  static inline auto GetName(const Kind kind) -> std::string_view {
    switch (kind) {
#define DEFINE_CASE(Name, Spelling) \
  case k##Name:                     \
    return #Name;
      FOR_EACH_TOKEN(DEFINE_CASE)
#undef DEFINE_CASE
      default:
        return "Unknown";
    }
  }

  static inline auto GetSpelling(const Kind kind) -> std::string_view {
    switch (kind) {
#define DEFINE_CASE(Name, Spelling) \
  case k##Name:                     \
    return Spelling;
      FOR_EACH_TOKEN(DEFINE_CASE)
#undef DEFINE_CASE
      default:
        return "<unknown>";
    }
  }

  static inline auto IsKeyword(const Kind kind) -> bool {
    switch (kind) {
#define DEFINE_CASE(Name, Spelling) case k##Name:
      FOR_EACH_KEYWORD_TOKEN(DEFINE_CASE)
#undef DEFINE_CASE
      return true;
      default:
        return false;
    }
  }

  // How a token kind reads in "expected ...": `=>`, an identifier, ...
  static inline auto Describe(const Kind kind) -> std::string {
    switch (kind) {
      case kIdent:
        return "a name";
      case kUpperIdent:
        return "a capitalized name";
      case kRawIdent:
        return "a name in backticks";
      case kNumber:
        return "a number";
      case kString:
        return "a string";
      case kColorLit:
        return "a color";
      case kPxLit:
      case kPctLit:
      case kFrLit:
        return "a unit value";
      case kDirective:
        return "a directive";
      case kBinMinus:
      case kMinus:
        return "`-`";
      case kEof:
        return "the end of the file";
      case kInvalid:
        return "an invalid token";
      case kUnderscore:
        return "`_`";
      default:
        return "`" + std::string(GetSpelling(kind)) + "`";
    }
  }

  static inline auto Set(std::initializer_list<Kind> kinds) -> KindSet {
    KindSet set{};
    for (const auto kind : kinds)
      set.set(kind);
    return set;
  }

  Kind kind = kInvalid;
  Span span{};
  // Set by the lexer on a token it had to report a problem with and cut short
  // (a string or backtick name that is never closed). The parser doesn't pile
  // its own complaint on top of it.
  bool malformed = false;

  constexpr auto Is(const Kind rhs) const -> bool {
    return kind == rhs;
  }

  auto Test(const KindSet& set) const -> bool {
    return set.test(kind);
  }

  friend auto operator<<(std::ostream& stream, const Token& rhs) -> std::ostream& {
    return stream << GetName(rhs.kind) << rhs.span;
  }
};
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_TOKEN_H
