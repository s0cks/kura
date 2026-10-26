#include "node_visitor.h"

#include "backend/ir.h"
#include "element.h"
#include "frontend/expr.h"
#include "frontend/expr_printer.h"
#include "gir.h"

namespace kura {
auto NodeVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  const auto& children = expr->GetChildren();
  if (children.empty()) {
    if (WantsValue())
      ReturnValue(NoneValue());
    return true;
  }

  for (auto idx = 0; idx < children.size() - 1; idx++) {
    expr::Expr* e = children[idx];
    if (!e)
      continue;

    NodeVisitor for_node(GetOwner());
    if (!e->Accept(&for_node))
      return false;
  }

  const auto last = children.back();
  if (!WantsValue()) {
    if (!last)
      return false;

    NodeVisitor for_node(GetOwner());
    if (!last->Accept(&for_node))
      return false;
  }

  if (!last) {
    Fail("missing expression");
    return false;
  }

  NodeVisitor for_expr(GetOwner());
  if (!last->Accept(&for_expr))
    return false;
  return true;
}

void NodeVisitor::EmitRenderCall(Definition* defn) {
#ifdef KURA_DEBUG
  auto value = Bind(defn);
  Emit(CheckRendererStatusInstr::New(value));
#else
  Emit(defn);
#endif  // KURA_DEBUG
}

auto NodeVisitor::VisitButton(expr::NodeExpr* node) -> VisitResult {
  EmitRenderCall(FillRectInstr::New());

  const auto& children = node->GetChildren();
  for (const auto& child : children) {
    if (!child)
      return false;

    NodeVisitor for_node(GetOwner());
    if (!child->Accept(&for_node))
      return false;
  }

  return true;
}

auto NodeVisitor::VisitText(expr::NodeExpr* node) -> VisitResult {
  Value* value = nullptr;

  for (const auto& property : node->GetProperties()) {
    if (property->GetProperty()->GetPropertyName()->GetValue() == "value") {
      ValueVisitor for_value(GetOwner());
      if (!property->GetValue()->Accept(&for_value))
        return false;
      value = for_value.GetValue();
      break;
    }
  }

  if (!value)
    value = Bind(ConstantInstr::New(String::New("")));

  EmitRenderCall(DrawTextInstr::New(value));
  return true;
}

auto NodeVisitor::VisitNode(expr::NodeExpr* expr) -> VisitResult {
  switch (expr->GetKind()) {
    case expr::NodeExpr::kButton:
      return VisitButton(expr);
    case expr::NodeExpr::kText:
      return VisitText(expr);
    default:
      return ValueVisitor::VisitNode(expr);
  }
}

auto NodeVisitor::EvalArm(expr::Expr* expr, Value** value) -> bool {
  *value = nullptr;
  if (!WantsValue())
    return EvalForEffect(expr);

  if (!expr) {
    *value = NoneValue();
    return true;
  }

  NodeVisitor for_value(GetOwner());
  if (!expr->Accept(&for_value))
    return false;

  *value = for_value.GetValue();
  if (!for_value.HasValue())
    (*value) = NoneValue();
  return true;
}
}  // namespace kura
