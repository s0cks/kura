#include "flow_graph_builder.h"

#include <iostream>

#include "common.h"
#include "expr.h"
#include "flow_graph.h"
#include "ir.h"

namespace kura {
static inline void AppendFragment(TargetEntryInstr* target, const EffectVisitor& vis) {
  Instruction::Link(target, vis.GetEntry());
}

// TODO(@s0cks): fix this, target_ and graph_ are dependent on strut position to
// initialize correctly
FlowGraphBuilder::FlowGraphBuilder() :
  target_(CreateNewBlock()),
  graph_(GraphEntryInstr::New(target_)) {}

auto FlowGraphBuilder::Build() -> FlowGraph* {
  return FlowGraph::New(graph_);
}

auto FlowGraphBuilder::Visit(expr::SeqExpr* expr) -> VisitResult {
  ValueVisitor for_expr(this);
  if (!for_expr(expr))
    return VisitResult::kStop;

  if (!for_expr.IsEmpty())
    Instruction::Link(target_, for_expr.GetEntry());

  return VisitResult::kContinue;
}

auto EffectVisitor::VisitStoreLocal(expr::StoreLocalExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  for (const auto& child : expr->GetChildren()) {
    EffectVisitor for_effect(GetOwner());
    if (!for_effect(child))
      return VisitResult::kStop;
    Append(for_effect);
  }

  return VisitResult::kContinue;
}

#define CREATE_TARGET_FRAGMENT(Name, Expression) \
  TargetEntryInstr* Name##_entry = nullptr;      \
  const auto Name##_expr = (Expression);         \
  if (Name##_expr) {                             \
    EffectVisitor for_##Name(GetOwner());        \
    if (!for_##Name(Name##_expr))                \
      return VisitResult::kStop;                 \
    Name##_entry = GetOwner()->CreateNewBlock(); \
    AppendFragment(Name##_entry, for_##Name);    \
  }

#define CREATE_VALUE_FRAGMENT(Name, Expression) \
  ValueVisitor for_##Name(GetOwner());          \
  const auto Name##_expr = (Expression);        \
  Value* Name##_value = nullptr;                \
  if ((Name##_expr)) {                          \
    if (!for_##Name(Name##_expr))               \
      return VisitResult::kStop;                \
    Append(for_##Name);                         \
    Name##_value = for_##Name.GetValue();       \
  }

auto EffectVisitor::VisitIf(expr::IfExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  CREATE_VALUE_FRAGMENT(cond, expr->GetCondition());
  CREATE_TARGET_FRAGMENT(then, expr->GetThen());
  CREATE_TARGET_FRAGMENT(else, expr->GetElse());
  AddBranch(for_cond, then_entry, else_entry, join);
  SetExit(join);
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  CREATE_VALUE_FRAGMENT(subject, expr->GetSubject());

  for (const auto& arm : expr->GetCases()) {
    CREATE_VALUE_FRAGMENT(pattern, arm.pattern);
    const auto test = Bind(BinaryOpInstr::NewEq(for_subject, for_pattern));
    CREATE_TARGET_FRAGMENT(body, arm.body);
    body_entry->AppendGoto(join);
    AddBranch(test, body_entry, join);
  }

  if (expr->HasWildcard()) {
    EffectVisitor for_wildcard(GetOwner());
    if (!for_wildcard(expr->GetWildcard()))
      return VisitResult::kStop;
    Append(for_wildcard);
    for_wildcard.AddGoto(join);
  }

  SetExit(join);
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitNode(expr::NodeExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitPipeline(expr::PipelineExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitList(expr::ListExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitRecordProperty(expr::RecordPropertyExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitSpread(expr::SpreadExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitWildcardPattern(expr::WildcardPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitLiteralPattern(expr::LiteralPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::ProcessValueList(const expr::ExprList& expressions, ValueList& values) -> VisitResult {
  for (const auto value : expressions) {
    CREATE_VALUE_FRAGMENT(value, value);
    if (!value_value)
      return VisitResult::kStop;
    values.push_back(value_value);
  }

  return VisitResult::kContinue;
}

auto EffectVisitor::ProcessValueList(expr::DynamicTemplateExpr& expr, ValueList& values) -> VisitResult {
  for (auto idx = 0; idx < expr.GetNumberOfChildren(); idx++) {
    const auto value = expr.GetChildAt(idx);
    if (!value)
      return VisitResult::kStop;
    CREATE_VALUE_FRAGMENT(value, value);
    if (!value_value)
      return VisitResult::kStop;
    values.push_back(for_value.GetValue());
  }

  return VisitResult::kContinue;
}

auto ValueVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  for (auto idx = 0; idx < expr->GetNumberOfChildren() - 1; idx++) {
    const auto child = expr->GetChildAt(idx);
    EffectVisitor for_effect(GetOwner());
    if (!for_effect(child))
      return VisitResult::kStop;
    Append(for_effect);
  }

  ValueVisitor for_last(GetOwner());
  if (!for_last(expr->GetChildAt(expr->GetNumberOfChildren() - 1)))
    return VisitResult::kStop;
  Append(for_last);
  ReturnValue(for_last);
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  ValueList args{};
  if (!ProcessValueList(expr->GetArgs(), args))
    return VisitResult::kStop;

  CREATE_VALUE_FRAGMENT(target, expr->GetTarget());
  ReturnDefinition(CallInstr::New(for_target, std::move(args)));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  ReturnDefinition(ConstantInstr::New(expr->GetValue()));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  CREATE_VALUE_FRAGMENT(value, expr->GetValue());
  ReturnDefinition(UnaryOpInstr::New(expr->GetOp(), for_value));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  CREATE_VALUE_FRAGMENT(left, expr->GetLeft());
  CREATE_VALUE_FRAGMENT(right, expr->GetRight());
  ReturnDefinition(BinaryOpInstr::New(expr->GetOp(), for_left, for_right));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  const auto record = Bind(NewRecordInstr::New());
  for (const auto& prop : expr->GetProperties()) {
    CREATE_VALUE_FRAGMENT(value, prop->GetValue());
    // TODO(@s0cks): need to create a property with a dynamic id
    AddStoreProperty(record, prop->GetProperty(), for_value);
  }

  ReturnValue(record);
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult {
  ReturnDefinition(LoadLocalInstr::New(expr->GetLocal()));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitList(expr::ListExpr* expr) -> VisitResult {
  ValueList values{};
  if (!ProcessValueList(*expr, values))
    return VisitResult::kStop;
  ReturnDefinition(NewListInstr::New(std::move(values)));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitLiteralPattern(expr::LiteralPatternExpr* expr) -> VisitResult {
  // TODO(@s0cks): refactor
  return VisitLiteral(expr->GetLiteral());
}

auto ValueVisitor::VisitWildcardPattern(expr::WildcardPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
  const auto join = GetOwner()->CreateNewBlock<JoinEntryInstr>();
  CREATE_VALUE_FRAGMENT(subject, expr->GetSubject());

  for (const auto& arm : expr->GetCases()) {
    CREATE_VALUE_FRAGMENT(pattern, arm.pattern);
    const auto test = Bind(BinaryOpInstr::NewEq(for_subject, for_pattern));

    const auto target = GetOwner()->CreateNewBlock();
    ValueVisitor for_arm(GetOwner());
    if (!for_arm(arm.body))
      return VisitResult::kStop;
    for_arm.AddGoto(join);
    AppendFragment(target, for_arm);

    AddBranch(test, target, join);
  }

  if (expr->HasWildcard()) {
    ValueVisitor for_wildcard(GetOwner());
    if (!for_wildcard(expr->GetWildcard()))
      return VisitResult::kStop;
    for_wildcard.AddGoto(join);
    Append(for_wildcard);
  }

  Add(join);
  return VisitResult::kContinue;
}
}  // namespace kura
