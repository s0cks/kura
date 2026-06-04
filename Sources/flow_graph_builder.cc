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
  target_(TargetEntryInstr::New()),
  graph_(GraphEntryInstr::New(target_)) {}

auto FlowGraphBuilder::Build() -> FlowGraph* {
  return FlowGraph::New(graph_);
}

auto FlowGraphBuilder::Visit(expr::Expr* expr) -> VisitResult {
  EffectVisitor for_value(this);
  if (!expr->Accept(&for_value))
    return VisitResult::kStop;

  // TODO(@s0cks): probably need to check if the EffectVisitor has been closed
  // if (!for_value.IsClosed())
  //   return VisitResult::kStop;

  if (!for_value.IsEmpty())
    Instruction::Link(target_, for_value.GetEntry());
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
  Value* value = nullptr;
  expr::Expr* last = nullptr;
  ValueVisitor for_last(GetOwner());
  const auto num_children = expr->GetNumberOfChildren();
  switch (num_children) {
    case 0: {
      value = Value::New(ConstantInstr::New(Bool::True()));
      goto emit_value;
    }
    case 1: {
      last = expr->GetChildAt(0);
      goto emit_last;
    }
  }

  for (auto idx = 0; idx < num_children - 1; idx++) {
    EffectVisitor for_effect(GetOwner());
    if (!for_effect(expr->GetChildAt(idx)))
      return VisitResult::kStop;
    Append(for_effect);
  }

emit_last:
  if (!for_last(last))
    return VisitResult::kStop;
  Append(for_last);
  value = for_last.GetValue();
emit_value:
  AddReturnExit(value);
  return VisitResult::kContinue;
}

#define CREATE_TARGET_FRAGMENT(Name, Expression) \
  TargetEntryInstr* Name##_entry = nullptr;      \
  const auto Name##_expr = (Expression);         \
  if (Name##_expr) {                             \
    EffectVisitor for_##Name(GetOwner());        \
    if (!for_##Name(Name##_expr))                \
      return VisitResult::kStop;                 \
    Name##_entry = TargetEntryInstr::New();      \
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
  CREATE_VALUE_FRAGMENT(cond, expr->GetCondition());
  CREATE_TARGET_FRAGMENT(then, expr->GetThen());
  CREATE_TARGET_FRAGMENT(else, expr->GetElse());
  SetExit(AddBranch(for_cond, then_entry, else_entry));
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
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

auto EffectVisitor::VisitIdentifierPattern(expr::IdentifierPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitVariantPattern(expr::VariantPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitRecordPattern(expr::RecordPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto EffectVisitor::VisitSeqPattern(expr::SeqPatternExpr* expr) -> VisitResult {
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
    const auto name = prop->GetPropertyName();
    CREATE_VALUE_FRAGMENT(value, prop->GetValue());
    // TODO(@s0cks): need to create a property with a dynamic id
    const auto property = Property::New(0, String::New(std::move(name)));  // TODO(@s0cks): get property at parse time?
    AddStoreProperty(record, property, for_value);
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
}  // namespace kura
