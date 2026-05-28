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

auto SeqExpr::VisitChildren(const std::function<bool(Expr*)> vis) -> bool {
  for (const auto& child : children_) {
    if (!vis(child))
      return false;
  }
  return true;
}

auto SeqExpr::VisitChildren(ExprVisitor* vis) -> bool {
  for (const auto& child : children_) {
    if (!child->Accept(vis))
      return false;
  }
  return true;
}

auto UnaryExpr::VisitChildren(ExprVisitor* vis) -> bool {
  return value_->Accept(vis);
}

auto UnaryExpr::VisitChildren(const std::function<bool(Expr*)> vis) -> bool {
  return vis(value_);
}

auto BinaryExpr::VisitChildren(ExprVisitor* vis) -> bool {
  if (!left_->Accept(vis))
    return false;
  if (!right_->Accept(vis))
    return false;
  return true;
}

auto BinaryExpr::VisitChildren(const std::function<bool(Expr*)> vis) -> bool {
  if (!vis(left_))
    return false;
  if (!vis(right_))
    return false;
  return true;
}

auto IfExpr::VisitChildren(ExprVisitor* vis) -> bool {
  if (!condition_->Accept(vis))
    return false;
  if (!then_->Accept(vis))
    return false;
  if (HasElseExpr() && !else_->Accept(vis))
    return false;
  return true;
}

auto IfExpr::VisitChildren(const std::function<bool(Expr*)> vis) -> bool {
  if (!vis(condition_))
    return false;
  if (!vis(then_))
    return false;
  if (HasElseExpr() && !vis(else_))
    return false;
  return true;
}

#define DEFINE_ACCEPT(Name)                                 \
  auto Name##Pattern::Accept(PatternVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                          \
  }
FOR_EACH_PATTERN(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto MatchExpr::VisitPatterns(PatternVisitor* vis) -> bool {
  for (const auto& pattern : patterns_) {
    if (!pattern->Accept(vis))
      return false;
  }
  return true;
}

auto MatchExpr::VisitPatterns(const std::function<bool(Pattern*)> vis) -> bool {
  for (const auto& pattern : patterns_) {
    if (!vis(pattern))
      return false;
  }
  return true;
}
}  // namespace kura::expr
