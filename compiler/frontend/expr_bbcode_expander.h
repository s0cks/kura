#ifndef KURA_BBCODE_EXPANDER_H
#define KURA_BBCODE_EXPANDER_H

#include "common.h"
#include "compiler/frontend/expr.h"

namespace kura {
class BBCodeExpander : public expr::ExprVisitor {
 private:
  expr::Expr* result_ = nullptr;

 public:
  BBCodeExpander() = default;
  ~BBCodeExpander() override = default;

#define DEFINE_VISIT(Name) auto Visit##Name(expr::Name##Expr*) -> VisitResult override;

  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT

  auto Expand(expr::NodeExpr& rhs) -> VisitResult;

  auto GetResult() const -> expr::Expr* {
    return result_;
  }

  inline auto HasResult() const -> bool {
    return GetResult() != nullptr;
  }

  void SetResult(expr::Expr* rhs) {
    result_ = rhs;
  }

 public:
  static inline auto ExpandExpr(expr::NodeExpr& rhs) -> expr::Expr* {
    // TODO(@s0cks): check rhs
    BBCodeExpander expander{};
    const auto status = expander.Expand(rhs);
    if (!status)
      return nullptr;

    return expander.GetResult();
  }
};
}  // namespace kura

#endif  // KURA_BBCODE_EXPANDER_H
