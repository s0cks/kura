#include "frontend/syntax/lexer.h"

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
          {"black", Token::kColorLit},   {"white", Token::kColorLit},   {"red", Token::kColorLit},
      {"green", Token::kColorLit},   {"blue", Token::kColorLit},    {"yellow", Token::kColorLit},
      {"cyan", Token::kColorLit},    {"magenta", Token::kColorLit}, {"gray", Token::kColorLit},
      {"orange", Token::kColorLit},  {"purple", Token::kColorLit},  {"transparent", Token::kColorLit},
  };
  return kKeywords;
}

class Lexer {
 private:
  const SourceFile& source_;
  const std::string& text_;
  DiagnosticList& diagnostics_;
  LexResult result_{};
  uint32_t pos_ = 0;

  auto Peek(const uint32_t offset = 0) const -> char {
    const auto idx = pos_ + offset;
    return idx < text_.size() ? text_[idx] : '\0';
  }

  auto AtEnd() const -> bool {
    return pos_ >= text_.size();
  }

  void Emit(const Token::Kind kind, const uint32_t begin, const bool malformed = false) {
    Token token{};
    token.kind = kind;
    token.span = Span(begin, pos_);
    token.malformed = malformed;
    result_.tokens.push_back(token);
  }

  auto Error(const DiagnosticCode code, const Span& span, std::string message, std::string label = {})
      -> Diagnostic& {
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
    return std::min<uint32_t>(len, static_cast<uint32_t>(text_.size()) - pos_);
  }

  // The `-` at pos_ is a binary minus when whitespace is on both sides of it.
  auto IsBinaryMinus() const -> bool {
    return pos_ > 0 && IsSpace(text_[pos_ - 1]) && IsSpace(Peek(1));
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

  void LexIdent() {
    const auto begin = pos_;
    const auto upper = IsUpper(Peek());
    while (IsIdentChar(Peek()))
      pos_++;
    // kebab-case: a `-` continues the name only when a name character follows.
    while (!upper && Peek() == '-' && IsIdentChar(Peek(1))) {
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

  void LexString() {
    const auto begin = pos_;
    auto malformed = false;
    pos_++;  // opening quote
    while (true) {
      const auto c = Peek();
      if (AtEnd() || c == '\n' || c == '\r') {
        malformed = true;
        Error(DiagnosticCode::kUnterminatedString, Span(begin, begin + 1), "this string is never closed",
              "string starts here")
            .WithHelp("add a closing `\"` before the end of the line; strings can't span lines");
        break;
      }
      if (c == '"') {
        pos_++;
        break;
      }
      if (c == '\\') {
        const auto next = Peek(1);
        if (next == 'n' || next == 't' || next == '"' || next == '\\') {
          pos_ += 2;
          continue;
        }
        const auto len = (AtEnd() || Peek(1) == '\0' || next == '\n' || next == '\r') ? 1U : 2U;
        Error(DiagnosticCode::kInvalidEscape, Span(pos_, pos_ + len), "unknown escape sequence",
              "not a valid escape")
            .WithHelp("the escapes are `\\n`, `\\t`, `\\\"` and `\\\\`");
        pos_ += len;
        continue;
      }
      pos_++;
    }
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
        return one(Token::kPlus);
      case '*':
        return one(Token::kStar);
      case '/':
        return one(Token::kSlash);
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
        return n == '=' ? two(Token::kLe) : one(Token::kLt);
      case '>':
        return n == '=' ? two(Token::kGe) : one(Token::kGt);
      case '.':
        if (n == '.' && Peek(2) == '.') {
          pos_ += 3;
          return Emit(Token::kSpread, begin);
        }
        return one(Token::kDot);
      case ',':
        return one(Token::kComma);
      case ':':
        return one(Token::kColon);
      case '?':
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
  Lexer(const SourceFile& source, DiagnosticList& diagnostics) :
    source_(source),
    text_(source.GetText()),
    diagnostics_(diagnostics) {}

  auto Run() -> LexResult {
    result_.tokens.reserve(text_.size() / 3 + 1);
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
  return Lexer(source, diagnostics).Run();
}
}  // namespace kura::syntax
