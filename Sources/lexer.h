#ifndef KURA_LEXER_H
#define KURA_LEXER_H

#include "source_pos.h"
#include "token.h"
#include <cstdio>
#include <vector>

namespace kura {
class Lexer {
private:
  Token previous_token_;
  Token current_token_;
  Token peek_token_;
  SourcePos pos_;
  std::vector<char> data_;
  uint64_t rpos_;

  inline auto PeekChar(const uint64_t offset = 0) const -> char {
    const auto idx = (rpos_ + offset);
    if (idx >= data_.size())
      return EOF;
    return data_[idx];
  }

  inline constexpr auto IsWhitespaceChar(const char c) -> bool {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == EOF;
  }

  inline void SkipWhitespace() {
    while (IsWhitespaceChar(PeekChar()))
      NextChar();
  }

  inline void Advance(uint64_t n = 1) {
    while (n-- > 0)
      NextChar();
  }

  inline char AdvanceUntil(const char expected) {
    uint64_t advance = 0;
    do {
      const auto next = PeekChar(advance + 1);
      if (next == expected) {
        rpos_ += advance;
        return expected;
      }

      switch (PeekChar()) {
      case '\0':
      case EOF:
        return EOF;
      }
    } while (true);
  }

  inline auto NextChar() -> char {
    const auto idx = rpos_ + 1;
    if (idx > data_.size())
      return EOF;
    const auto next = data_[idx];
    switch (next) {
    case '\n':
      pos_.row += 1;
      pos_.col = 1;
      break;
    default:
      pos_.col += 1;
    }
    return static_cast<char>(next);
  }

  inline auto InvalidToken(const char token) -> const Token & {
    return current_token_ = {
               .kind = Token::kInvalidToken,
               .start = pos_,
               .end = pos_,
           };
  }

  inline auto NextToken(const Token::Kind kind, const char token)
      -> const Token & {
    return current_token_ = {
               .kind = kind,
               .start = pos_,
               .end = pos_,
           };
  }

  inline auto NextToken(const Token::Kind kind, const SourcePos start,
                        const SourcePos end) -> const Token & {
    return current_token_ = {
               .kind = kind,
               .start = std::move(start),
               .end = std::move(end),
           };
  }

public:
  Lexer() = default;
  ~Lexer() = default;

  auto HasNext() const -> bool {
    const auto idx = (rpos_ + 1);
    return idx < data_.size();
  }

  auto Next() -> const Token &;
};

} // namespace kura

#endif // KURA_LEXER_H
