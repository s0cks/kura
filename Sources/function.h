#ifndef KURA_FUNCTION_H
#define KURA_FUNCTION_H

#include <string>

#include "expr.h"
#include "ir.h"

namespace kura {
struct Function {
  std::string name{};
  expr::Expr* body = nullptr;
  GraphEntryInstr* entry = nullptr;

  Function() = delete;
  Function(const std::string n) :
    name(std::move(n)) {}
  ~Function() = delete;

  inline auto HasBody() const -> bool {
    return body != nullptr;
  }

  inline auto HasEntry() const -> bool {
    return entry != nullptr;
  }

  auto ToString() const -> std::string;

  static inline auto New(const std::string name) -> Function* {
    return new Function(std::move(name));
  }
};
}  // namespace kura

#endif  // KURA_FUNCTION_H
