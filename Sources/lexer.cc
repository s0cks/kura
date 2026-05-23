#include "lexer.h"

namespace kura {
auto Lexer::Next() -> const Token & {
  if (!peek_token_.IsInvalid()) {
    std::swap(previous_token_, current_token_);
    std::swap(current_token_, peek_token_);
    peek_token_ = Token{};
    return current_token_;
  }

  const auto next = PeekChar();
  switch (next) {
  case '+':
    Advance();
    return NextToken(Token::kPlus, next);
  case '-':
    Advance();
    return NextToken(Token::kMinus, next);
  case '*':
    Advance();
    return NextToken(Token::kStar, next);
  case '/':
    Advance();
    return NextToken(Token::kForwardSlash, next);
  default:
    return InvalidToken(next);
  }

  return current_token_;
}
} // namespace kura
