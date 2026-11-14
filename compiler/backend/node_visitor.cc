#include "node_visitor.h"

#include "backend/dom_lowering.h"
#include "backend/ir.h"
#include "dom/builder.h"
#include "frontend/expr.h"

namespace kura {
auto NodeVisitor::Render(expr::Expr* expr) -> VisitResult {
  dom::Tree tree{};
  dom::Builder builder(&tree, [this](LocalVariable* local) {
    return GetOwner()->FoldableLocal(local);
  });
  builder.SetFunctionResolver([this](LocalVariable* local) {
    return GetOwner()->FindFunction(local);
  });
  builder.Build(expr);
  if (!builder.Ok()) {
    for (const auto& error : builder.GetErrors())
      Fail(error);
    return false;
  }

  GetOwner()->DumpDom(tree.GetRoot());
  DomLowering lowering(this);
  return lowering.Lower(tree.GetRoot());
}

auto NodeVisitor::VisitNode(expr::NodeExpr* expr) -> VisitResult {
  return Render(expr);
}

auto NodeVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  if (expr->GetChildren().empty()) {
    if (WantsValue())
      ReturnValue(NoneValue());

    return true;
  }
  return Render(expr);
}

// A call to a helper that returns nodes is rendered, as the nodes it returns; any other call is a value.
auto NodeVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  dom::Tree tree{};
  dom::Builder probe(&tree);
  probe.SetFunctionResolver([this](LocalVariable* local) {
    return GetOwner()->FindFunction(local);
  });
  if (probe.IsHelperCall(expr))
    return Render(expr);
  return ValueVisitor::VisitCall(expr);
}

auto NodeVisitor::VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult {
  const auto iterable = Eval(expr->GetIterable());
  if (!iterable)
    return false;

  Loop loop{};
  if (!BeginLoop(iterable, &loop))
    return false;
  // `for i in xs` skips the element, and `for _, x in xs` the index
  if (expr->GetLocal() != nullptr)
    GetOwner()->Define(expr->GetLocal(), loop.item->GetDefinition());
  if (expr->GetIndex() != nullptr)
    GetOwner()->Define(expr->GetIndex(), loop.index);

  const auto render = [&]() -> bool {
    NodeVisitor for_body(GetOwner());
    return expr->GetBody()->Accept(&for_body);
  };

  if (expr->HasClause()) {
    const auto condition = Eval(expr->GetClause());
    if (!condition)
      return false;
    const auto keep = GetOwner()->NewTarget("guard.keep");
    const auto skip = GetOwner()->NewTarget("guard.skip");
    const auto latch = GetOwner()->NewJoin("guard.latch");
    GetOwner()->Terminate(BranchInstr::New(condition, keep, skip, latch));
    GetOwner()->Enter(keep);
    if (!render())
      return false;
    GetOwner()->Terminate(GotoInstr::New(latch));
    GetOwner()->Enter(skip);
    GetOwner()->Terminate(GotoInstr::New(latch));
    GetOwner()->Enter(latch);
  } else if (!render()) {
    return false;
  }
  EndLoop(&loop);

  if (WantsValue())
    ReturnValue(NoneValue());
  return true;
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
