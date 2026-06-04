#ifndef KURA_FUNCTION_H
#define KURA_FUNCTION_H

#include <string>

#include "common.h"
#include "object.h"

namespace kura {
class GraphEntryInstr;
namespace expr {
class Expr;
class SeqExpr;
class ExprBuilder;
}  // namespace expr

class Function {
  friend class expr::ExprBuilder;
  DEFINE_NON_COPYABLE_TYPE(Function);

 private:
  String* name_;
  expr::SeqExpr* body_ = nullptr;
  GraphEntryInstr* entry_ = nullptr;

  void SetBody(expr::SeqExpr* rhs) {
    body_ = rhs;
  }

 public:
  explicit Function(String* name) :
    name_(name) {}
  ~Function() = default;

  auto GetName() const -> String* {
    return name_;
  }

  auto GetBody() const -> expr::SeqExpr* {
    return body_;
  }

  inline auto HasBody() const -> bool {
    return GetBody() != nullptr;
  }

  auto GetEntry() const -> GraphEntryInstr* {
    return entry_;
  }

  inline auto HasEntry() const -> bool {
    return GetEntry() != nullptr;
  }

  auto ToString() const -> std::string;

 public:
  static inline auto New(String* name) -> Function* {
    return new Function(name);
  }

  static inline auto New(const std::string name) -> Function* {
    return New(String::New(std::move(name)));
  }
};
}  // namespace kura

#endif  // KURA_FUNCTION_H
