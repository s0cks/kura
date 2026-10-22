#include "backend/effect_visitor.h"

#include "backend/value_visitor.h"

namespace kura {
auto EffectVisitor::VisitStoreLocal(expr::StoreLocalExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitGetProperty(expr::GetPropertyExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  for (const auto& child : expr->GetChildren()) {
    EffectVisitor for_effect(GetOwner());
    if (!for_effect(child))
      return false;
    Append(for_effect);
  }

  return true;
}

auto EffectVisitor::VisitIf(expr::IfExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  AddLabel("cond_test");
  CREATE_VALUE_FRAGMENT(cond, expr->GetCondition());
  Append(for_cond);

  CREATE_TARGET_FRAGMENT(then, expr->GetThen());
  for_then.AddGoto(join);
  then_entry->SetLabel(String::New("then"));

  CREATE_TARGET_FRAGMENT(else, expr->GetElse());
  for_else.AddGoto(join);
  else_entry->SetLabel(String::New("else"));

  AddBranch(for_cond, then_entry, else_entry, join);
  SetExit(join);
  return true;
}

auto EffectVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  CREATE_VALUE_FRAGMENT(subject, expr->GetSubject());

  for (const auto& arm : expr->GetCases()) {
    CREATE_VALUE_FRAGMENT(pattern, arm->GetPattern());
    const auto test = Bind(BinaryOpInstr::NewEq(for_subject, for_pattern));
    CREATE_TARGET_FRAGMENT(body, arm->GetBody());
    body_entry->AppendGoto(join);
#ifdef KURA_DEBUG
    body_entry->SetLabel(String::New("pattern"));
#endif  // KURA_DEBUG
    AddBranch(test, body_entry, join);
  }

  if (expr->HasWildcard()) {
    CREATE_EFFECT_FRAGMENT(wildcard, expr->GetWildcard());
    Append(for_wildcard);
    for_wildcard.AddGoto(join);
  }

  SetExit(join);
  return true;
}

auto EffectVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitNode(expr::NodeExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitPipeline(expr::PipelineExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitList(expr::ListExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitCase(expr::CaseExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitStoreProperty(expr::StorePropertyExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitSpread(expr::SpreadExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitWildcardPattern(expr::WildcardPatternExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::VisitLiteralPattern(expr::LiteralPatternExpr* expr) -> VisitResult {
  return true;
}

auto EffectVisitor::ProcessValueList(const expr::ExprList& expressions, ValueList& values) -> VisitResult {
  for (const auto value : expressions) {
    CREATE_VALUE_FRAGMENT(value, value);
    if (for_value.HasValue())
      values.push_back(for_value);
  }

  return true;
}

auto EffectVisitor::ProcessValueList(expr::DynamicTemplateExpr& expr, ValueList& values) -> VisitResult {
  for (auto idx = 0; idx < expr.GetNumberOfChildren(); idx++) {
    const auto value = expr.GetChildAt(idx);
    if (!value)
      return false;
    CREATE_VALUE_FRAGMENT(value, value);
    if (for_value.HasValue())
      values.push_back(for_value);
  }

  return true;
}
}  // namespace kura
