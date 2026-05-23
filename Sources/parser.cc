#include "parser.h"
#include <unordered_map>

namespace kura {
static std::unordered_map<std::string, Token::Kind> tokens_{};

void Parser::Init() {
  if (!tokens_.empty())
    return;
  tokens_.insert({"let", Token::kLet});
}
} // namespace kura
