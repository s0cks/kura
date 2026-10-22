#include "backend/value_visitor.h"

#include <sstream>

namespace kura {
auto ValueVisitor::VisitIf(expr::IfExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  AddLabel("cond_test");
  CREATE_VALUE_FRAGMENT(cond, expr->GetCondition());
  Append(for_cond);

  CREATE_TARGET_VALUE_FRAGMENT(then, expr->GetThen());
  for_then.AddGoto(join);
  then_entry->SetLabel(String::New("then"));

  CREATE_TARGET_VALUE_FRAGMENT(else, expr->GetElse());
  for_else.AddGoto(join);
  else_entry->SetLabel(String::New("else"));

  AddBranch(for_cond, then_entry, else_entry, join);

  const auto phi = join->NewPhi();
  phi->AddIncoming(then_entry, for_then);
  phi->AddIncoming(else_entry, for_else);
  join->Append(phi);
  SetExit(phi);
  ReturnDefinition(phi);
  return true;
}

auto ValueVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  for (auto idx = 0; idx < expr->GetNumberOfChildren() - 1; idx++) {
    const auto child = expr->GetChildAt(idx);
    CREATE_EFFECT_FRAGMENT(child, expr->GetChildAt(idx));
    Append(for_child);
  }

  ValueVisitor for_last(GetOwner());
  if (!for_last(expr->GetChildAt(expr->GetNumberOfChildren() - 1)))
    return false;

  Append(for_last);
  ReturnValue(for_last);
  return true;
}

auto ValueVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  ValueList args{};
  if (!ProcessValueList(expr->GetArgs(), args))
    return false;

  CREATE_VALUE_FRAGMENT(target, expr->GetTarget());
  Append(for_target);
  ReturnDefinition(CallInstr::New(for_target, std::move(args)));
  return true;
}

auto ValueVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  ReturnDefinition(ConstantInstr::New(expr->GetValue()));
  return true;
}

auto ValueVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  CREATE_VALUE_FRAGMENT(value, expr->GetValue());
  Append(for_value);
  ReturnDefinition(UnaryOpInstr::New(expr->GetOp(), for_value));
  return true;
}

auto ValueVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  CREATE_VALUE_FRAGMENT(left, expr->GetLeft());
  Append(for_left);
  CREATE_VALUE_FRAGMENT(right, expr->GetRight());
  Append(for_right);
  ReturnDefinition(BinaryOpInstr::New(expr->GetOp(), for_left, for_right));
  return true;
}

auto ValueVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  const auto record = Bind(NewRecordInstr::New());
  for (const auto& prop : expr->GetProperties()) {
    CREATE_VALUE_FRAGMENT(value, prop->GetValue());
    Append(for_value);
    // TODO(@s0cks): need to create a property with a dynamic id
    AddStoreProperty(record, prop->GetProperty(), for_value);
  }

  ReturnValue(record);
  return true;
}

auto ValueVisitor::VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult {
  ReturnDefinition(LoadLocalInstr::New(expr->GetLocal()));
  return true;
}

auto ValueVisitor::VisitList(expr::ListExpr* expr) -> VisitResult {
  ValueList values{};
  if (!ProcessValueList(*expr, values))
    return false;
  ReturnDefinition(NewListInstr::New(std::move(values)));
  return true;
}

auto ValueVisitor::VisitLiteralPattern(expr::LiteralPatternExpr* expr) -> VisitResult {
  // TODO(@s0cks): refactor
  return VisitLiteral(expr->GetLiteral());
}

auto ValueVisitor::VisitWildcardPattern(expr::WildcardPatternExpr* expr) -> VisitResult {
  return true;
}

auto ValueVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  CREATE_VALUE_FRAGMENT(subject, expr->GetSubject());

  uint64_t idx = 0;
  for (const auto& arm : expr->GetCases()) {
#ifdef KURA_DEBUG
    std::stringstream ss{};
    ss << "pattern_" << (++idx);
    const auto prefix = ss.str();
    AddLabel(prefix + "_test");
#endif  // KURA_DEBUG
    CREATE_VALUE_FRAGMENT(pattern, arm->GetPattern());
    Append(for_pattern);
    const auto test = Bind(BinaryOpInstr::NewEq(for_subject, for_pattern));

    const auto target = GetOwner()->CreateNewBlock();
    ValueVisitor for_arm(GetOwner());
    if (!for_arm(arm->GetBody()))
      return false;

    for_arm.AddGoto(join);
    AppendFragment(target, for_arm);

#ifdef KURA_DEBUG
    target->SetLabel(String::New(prefix));
#endif  // KURA_DEBUG

    AddBranch(test, target, join);
  }

  if (expr->HasWildcard()) {
    CREATE_VALUE_FRAGMENT(wildcard, expr->GetWildcard());
#ifdef KURA_DEBUG
    AddLabel("Wildcard Pattern");
#endif  // KURA_DEBUG
    for_wildcard.AddGoto(join);
    Append(for_wildcard);
  }

  SetExit(join);
  return true;
}

auto ValueVisitor::VisitGetProperty(expr::GetPropertyExpr* expr) -> VisitResult {
  CREATE_VALUE_FRAGMENT(instance, expr->GetInstance());
  Append(for_instance);

  const auto property = expr->GetProperty();
  const auto load = expr->IsSafeAccess() ? CreateSafeLoad(for_instance, property) : CreateLoad(for_instance, property);
  ReturnValue(load);
  return true;
}
}  // namespace kura
