#include "expr.h"

namespace kura::expr {
#define DEFINE_ACCEPT(Name)                           \
  auto Name##Expr::Accept(ExprVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                    \
  }
FOR_EACH_EXPR(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto Expr::VisitChildren(ExprVisitor* vis) -> bool {
  return true;
}

auto Expr::VisitChildren(const std::function<bool(Expr*)> vis) -> bool {
  return true;
}
}  // namespace kura::expr
