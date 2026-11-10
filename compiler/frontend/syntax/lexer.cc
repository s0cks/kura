#include "compiler/frontend/syntax/lexer.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace kura::syntax {
namespace {
inline auto IsDigit(const char c) -> bool {
  return c >= '0' && c <= '9';
}

inline auto IsLower(const char c) -> bool {
  return c >= 'a' && c <= 'z';
}

inline auto IsUpper(const char c) -> bool {
  return c >= 'A' && c <= 'Z';
}

inline auto IsIdentChar(const char c) -> bool {
  return IsDigit(c) || IsLower(c) || IsUpper(c) || c == '_';
}

inline auto IsSpace(const char c) -> bool {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

auto Keywords() -> const std::unordered_map<std::string_view, Token::Kind>& {
  static const std::unordered_map<std::string_view, Token::Kind> kKeywords{
#define DEFINE_ENTRY(Name, Spelling) {Spelling, Token::k##Name},
      FOR_EACH_KEYWORD_TOKEN(DEFINE_ENTRY)
#undef DEFINE_ENTRY
          {"black", Token::kColorLit},
      {"white", Token::kColorLit},
      {"red", Token::kColorLit},
      {"green", Token::kColorLit},
      {"blue", Token::kColorLit},
      {"yellow", Token::kColorLit},
      {"cyan", Token::kColorLit},
      {"magenta", Token::kColorLit},
      {"gray", Token::kColorLit},
      {"orange", Token::kColorLit},
      {"purple", Token::kColorLit},
      {"transparent", Token::kColorLit},
  };
  return kKeywords;
}

class Lexer {
 private:
  const SourceFile& source_;
  const std::string& text_;
  DiagnosticList& diagnostics_;
  LexResult result_{};
  uint32_t begin_ = 0;
  uint32_t end_ = 0;
  uint32_t pos_ = 0;
  // Scanning a string inside an interpolation: if it is never closed, the interpolation is what is wrong
  // (`"total: {1 + 2"`), so the string doesn't report it itself.
  bool nested_ = false;

  auto Peek(const uint32_t offset = 0) const -> char {
    const auto idx = pos_ + offset;
    return idx < end_ ? text_[idx] : '\0';
  }

  auto AtEnd() const -> bool {
    return pos_ >= end_;
  }

  void Emit(const Token::Kind kind, const uint32_t begin, const bool malformed = false) {
    Token token{};
    token.kind = kind;
    token.span = Span(begin, pos_);
    token.malformed = malformed;
    result_.tokens.push_back(token);
  }

  auto Error(const DiagnosticCode code, const Span& span, std::string message, std::string label = {}) -> Diagnostic& {
    return diagnostics_.Error(code, span, std::move(message), std::move(label));
  }

  // The length in bytes of the UTF-8 sequence starting at pos_, at least one.
  auto CharLength() const -> uint32_t {
    const auto c = static_cast<unsigned char>(Peek());
    uint32_t len = 1;
    if (c >= 0xF0)
      len = 4;
    else if (c >= 0xE0)
      len = 3;
    else if (c >= 0xC0)
      len = 2;
    return std::min<uint32_t>(len, end_ - pos_);
  }

  // The `-` at pos_ is a binary minus when whitespace is on both sides of it.
  auto IsBinaryMinus() const -> bool {
    return pos_ > begin_ && IsSpace(text_[pos_ - 1]) && IsSpace(Peek(1));
  }

  void LexNumber() {
    const auto begin = pos_;
    while (IsDigit(Peek()))
      pos_++;
    if (Peek() == '.' && IsDigit(Peek(1))) {
      pos_++;
      while (IsDigit(Peek()))
        pos_++;
    }
    // The unit suffix wins over a plain number, exactly like the grammar's
    // longest match did: `10px` is one token, `10pxx` is `10px` then `x`.
    if (Peek() == 'p' && Peek(1) == 'x') {
      pos_ += 2;
      return Emit(Token::kPxLit, begin);
    }
    if (Peek() == 'f' && Peek(1) == 'r') {
      pos_ += 2;
      return Emit(Token::kFrLit, begin);
    }
    if (Peek() == '%') {
      pos_++;
      return Emit(Token::kPctLit, begin);
    }
    Emit(Token::kNumber, begin);
  }

  // True when the name part after the `-` at pos_ is nothing but digits: `x-1`, `n-20`.
  auto DigitsOnlyAfterHyphen() const -> bool {
    uint32_t len = 0;
    while (IsIdentChar(Peek(1 + len))) {
      if (!IsDigit(Peek(1 + len)))
        return false;
      len++;
    }
    return len > 0;
  }

  void LexIdent() {
    const auto begin = pos_;
    const auto upper = IsUpper(Peek());
    while (IsIdentChar(Peek()))
      pos_++;
    // kebab-case: a `-` continues the name only when a name character follows, and what follows isn't just
    // digits. `x-1` is `x` minus `1` (the parser then asks for spaces); `pos-2d-to-3d` is one name.
    while (!upper && Peek() == '-' && IsIdentChar(Peek(1)) && !DigitsOnlyAfterHyphen()) {
      pos_++;
      while (IsIdentChar(Peek()))
        pos_++;
    }
    const auto word = std::string_view(text_).substr(begin, pos_ - begin);
    if (word == "_")
      return Emit(Token::kUnderscore, begin);
    const auto found = Keywords().find(word);
    if (found != Keywords().end())
      return Emit(found->second, begin);
    Emit(upper ? Token::kUpperIdent : Token::kIdent, begin);
  }

  void LexDirective() {
    const auto begin = pos_;
    pos_++;  // '@'
    if (!IsLower(Peek())) {
      Error(DiagnosticCode::kBadDirective, Span(begin, pos_), "`@` must be followed by a directive name",
            "expected a name like `pure` here");
      return Emit(Token::kInvalid, begin);
    }
    const auto lower_or_digit = [](const char c) {
      return IsLower(c) || IsDigit(c);
    };
    while (lower_or_digit(Peek()))
      pos_++;
    while (Peek() == '-' && lower_or_digit(Peek(1))) {
      pos_++;
      while (lower_or_digit(Peek()))
        pos_++;
    }
    Emit(Token::kDirective, begin);
  }

  // Scans the rest of a string whose opening quote is already consumed, up to and including the closing
  // quote. A `{` starts an interpolation, which is scanned through to its matching `}` (strings nested in it
  // are scanned the same way, so `"a {f("b")} c"` is one token). Returns false when the string was cut
  // short at the end of the line; the problem has been reported by then.
  auto ScanStringBody(const uint32_t begin, bool& malformed) -> bool {
    while (true) {
      const auto c = Peek();
      if (AtEnd() || c == '\n' || c == '\r') {
        malformed = true;
        if (!nested_) {
          Error(DiagnosticCode::kUnterminatedString, Span(begin, begin + 1), "this string is never closed",
                "string starts here")
              .WithHelp("add a closing `\"` before the end of the line; strings can't span lines");
        }
        return false;
      }
      if (c == '"') {
        pos_++;
        return true;
      }
      if (c == '{') {
        if (!ScanInterpolation(malformed))
          return false;
        continue;
      }
      if (c == '\\') {
        const auto next = Peek(1);
        if (next == 'n' || next == 't' || next == '"' || next == '\\' || next == '{' || next == '}') {
          pos_ += 2;
          continue;
        }
        const auto len = (AtEnd() || Peek(1) == '\0' || next == '\n' || next == '\r') ? 1U : 2U;
        Error(DiagnosticCode::kInvalidEscape, Span(pos_, pos_ + len), "unknown escape sequence", "not a valid escape")
            .WithHelp("the escapes are `\\n`, `\\t`, `\\\"`, `\\\\`, `\\{` and `\\}`");
        pos_ += len;
        continue;
      }
      pos_++;
    }
  }

  // pos_ is at the `{` of an interpolation. Leaves it after the matching `}`.
  auto ScanInterpolation(bool& malformed) -> bool {
    const auto open = pos_;
    pos_++;  // '{'
    uint32_t depth = 1;
    while (depth > 0) {
      const auto c = Peek();
      if (AtEnd() || c == '\n' || c == '\r') {
        malformed = true;
        Error(DiagnosticCode::kBadInterpolation, Span(open, open + 1), "this `{` in a string is never closed",
              "interpolation starts here")
            .WithHelp("close it with `}`; to write a literal brace in a string, use `\\{`");
        return false;
      }
      if (c == '"') {
        const auto nested = pos_;
        pos_++;
        const auto outer = nested_;
        nested_ = true;
        const auto closed = ScanStringBody(nested, malformed);
        nested_ = outer;
        if (!closed) {
          Error(DiagnosticCode::kBadInterpolation, Span(open, open + 1), "this `{` in a string is never closed",
                "interpolation starts here")
              .WithHelp("close it with `}`; to write a literal brace in a string, use `\\{`");
          return false;
        }
        continue;
      }
      if (c == '{')
        depth++;
      else if (c == '}')
        depth--;
      pos_++;
    }
    return true;
  }

  void LexString() {
    const auto begin = pos_;
    auto malformed = false;
    pos_++;  // opening quote
    ScanStringBody(begin, malformed);
    Emit(Token::kString, begin, malformed);
  }

  void LexRawIdent() {
    const auto begin = pos_;
    auto malformed = false;
    pos_++;  // opening backtick
    while (!AtEnd() && Peek() != '`' && Peek() != '\n' && Peek() != '\r')
      pos_++;
    if (Peek() == '`' && pos_ > begin + 1) {
      pos_++;
    } else if (Peek() == '`') {
      // an empty ``
      pos_++;
      malformed = true;
      Error(DiagnosticCode::kUnterminatedRawIdent, Span(begin, pos_), "a name in backticks can't be empty");
    } else {
      malformed = true;
      Error(DiagnosticCode::kUnterminatedRawIdent, Span(begin, begin + 1), "this backtick name is never closed",
            "starts here")
          .WithHelp("add a closing backtick before the end of the line");
    }
    Emit(Token::kRawIdent, begin, malformed);
  }

  void LexMinus() {
    const auto begin = pos_;
    if (Peek(1) == '=' && Peek(2) != '=') {
      pos_ += 2;
      return Emit(Token::kMinusEq, begin);
    }
    const auto binary = IsBinaryMinus();
    pos_++;
    Emit(binary ? Token::kBinMinus : Token::kMinus, begin);
  }

  void LexPunctuation() {
    const auto begin = pos_;
    const auto c = Peek();
    const auto n = Peek(1);
    const auto two = [&](const Token::Kind kind) {
      pos_ += 2;
      Emit(kind, begin);
    };
    const auto one = [&](const Token::Kind kind) {
      pos_ += 1;
      Emit(kind, begin);
    };
    switch (c) {
      case '+':
        return n == '=' ? two(Token::kPlusEq) : one(Token::kPlus);
      case '*':
        return n == '=' ? two(Token::kStarEq) : one(Token::kStar);
      case '/':
        return n == '=' ? two(Token::kSlashEq) : one(Token::kSlash);
      case '|':
        if (n == '>')
          return two(Token::kPipe);
        if (n == '|')
          return two(Token::kOr);
        return one(Token::kBar);
      case '&':
        if (n == '&')
          return two(Token::kAnd);
        break;
      case '=':
        if (n == '=')
          return two(Token::kEq);
        if (n == '>')
          return two(Token::kFatArrow);
        return one(Token::kAssign);
      case '!':
        return n == '=' ? two(Token::kNeq) : one(Token::kBang);
      case '<':
        if (n == '|')
          return two(Token::kPipeBack);
        return n == '=' ? two(Token::kLe) : one(Token::kLt);
      case '>':
        return n == '=' ? two(Token::kGe) : one(Token::kGt);
      case '.':
        if (n == '.' && Peek(2) == '.') {
          pos_ += 3;
          return Emit(Token::kSpread, begin);
        }
        if (n == '.' && Peek(2) == '=') {
          pos_ += 3;
          return Emit(Token::kDotDotEq, begin);
        }
        if (n == '.')
          return two(Token::kDotDot);
        return one(Token::kDot);
      case ',':
        return one(Token::kComma);
      case ':':
        return one(Token::kColon);
      case '?':
        if (n == '?')
          return two(Token::kQQ);
        if (n == '.')
          return two(Token::kQDot);
        return one(Token::kQMark);
      case '#':
        return one(Token::kHash);
      case '(':
        return one(Token::kLParen);
      case ')':
        return one(Token::kRParen);
      case '{':
        return one(Token::kLBrace);
      case '}':
        return one(Token::kRBrace);
      case '[':
        return one(Token::kLBrack);
      case ']':
        return one(Token::kRBrack);
      case '%':
        return n == '=' ? two(Token::kPercentEq) : one(Token::kPercent);
      default:
        break;
    }
    // `&` alone, or something that isn't Kura at all
    pos_ += CharLength();
    Error(DiagnosticCode::kUnexpectedCharacter, Span(begin, pos_),
          "`" + std::string(source_.GetText(Span(begin, pos_))) + "` is not part of Kura's syntax",
          "unexpected character");
    Emit(Token::kInvalid, begin);
  }

 public:
  Lexer(const SourceFile& source, const uint32_t begin, const uint32_t end, DiagnosticList& diagnostics) :
    source_(source),
    text_(source.GetText()),
    diagnostics_(diagnostics),
    begin_(begin),
    end_(std::min<uint32_t>(end, static_cast<uint32_t>(source.GetText().size()))),
    pos_(begin) {}

  auto Run() -> LexResult {
    result_.tokens.reserve((end_ - begin_) / 3 + 1);
    while (!AtEnd()) {
      const auto c = Peek();
      if (IsSpace(c)) {
        pos_++;
      } else if (c == '/' && Peek(1) == '/') {
        const auto begin = pos_;
        while (!AtEnd() && Peek() != '\n')
          pos_++;
        result_.comments.emplace_back(begin, pos_);
      } else if (IsDigit(c)) {
        LexNumber();
      } else if (IsLower(c) || c == '_' || IsUpper(c)) {
        LexIdent();
      } else if (c == '@') {
        LexDirective();
      } else if (c == '"') {
        LexString();
      } else if (c == '`') {
        LexRawIdent();
      } else if (c == '-') {
        LexMinus();
      } else {
        LexPunctuation();
      }
    }
    result_.tokens.push_back(Token{Token::kEof, Span(pos_, pos_)});
    return std::move(result_);
  }
};
}  // namespace

auto Lex(const SourceFile& source, DiagnosticList& diagnostics) -> LexResult {
  return Lexer(source, 0, static_cast<uint32_t>(source.GetText().size()), diagnostics).Run();
}

auto LexRange(const SourceFile& source, const uint32_t begin, const uint32_t end, DiagnosticList& diagnostics)
    -> LexResult {
  return Lexer(source, begin, end, diagnostics).Run();
}
}  // namespace kura::syntax
