#include "expr.h"

namespace kura::expr {
#define DEFINE_ACCEPT(Name)                                  \
  auto Name##Expr::Accept(ExprVisitor* vis) -> VisitResult { \
    return vis->Visit##Name(this);                           \
  }
FOR_EACH_EXPR(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto RecordExpr::VisitSpreads(ExprVisitor* vis) -> bool {
  for (const auto& spread : spreads_) {
    if (!spread->Accept(vis))
      return false;
  }
  return true;
}

auto RecordExpr::VisitSpreads(const std::function<bool(Expr*)> vis) -> bool {
  for (const auto& spread : spreads_) {
    if (!vis(spread))
      return false;
  }
  return true;
}

auto RecordExpr::VisitProperties(ExprVisitor* vis) -> bool {
  for (const auto& property : properties_) {
    if (!property->Accept(vis))
      return false;
  }
  return true;
}

auto RecordExpr::VisitProperties(const std::function<bool(Expr*)> vis) -> bool {
  for (const auto& property : properties_) {
    if (!vis(property))
      return false;
  }
  return true;
}

auto RecordExpr::VisitChildren(ExprVisitor* vis) -> VisitResult {
  for (const auto& spread : spreads_) {
    if (!spread->Accept(vis))
      return VisitResult::kStop;
  }
  for (const auto& property : properties_) {
    if (!property->Accept(vis))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}

auto RecordExpr::VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult {
  for (const auto& spread : spreads_) {
    if (!vis(spread))
      return VisitResult::kStop;
  }
  for (const auto& property : properties_) {
    if (!vis(property))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}

auto CallExpr::VisitChildren(ExprVisitor* vis) -> VisitResult {
  if (!GetTarget()->Accept(vis))
    return VisitResult::kStop;
  for (const auto& arg : args_) {
    if (!arg->Accept(vis))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}
}  // namespace kura::expr
