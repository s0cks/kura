#include "flow_graph_builder.h"

#include <iostream>

#include "common.h"
#include "expr.h"
#include "flow_graph.h"
#include "ir.h"

namespace kura {
#ifndef NOT_IMPLEMENTED

// clang-format off
#define NOT_IMPLEMENTED \
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
// clang-format on

#endif  // NOT_IMPLEMENTED

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
  // TODO(@s0cks): account for multiple expressions involved
  ValueVisitor for_value(this);
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
    default:
      break;
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

auto EffectVisitor::VisitIf(expr::IfExpr* expr) -> VisitResult {
  ValueVisitor for_cond(GetOwner());
  if (!for_cond(expr->GetCondition()))
    return VisitResult::kStop;
  Append(for_cond);

  TargetEntryInstr* then_entry = TargetEntryInstr::New();
  TargetEntryInstr* else_entry = expr->HasElse() ? TargetEntryInstr::New() : nullptr;
  const auto join = Join(then_entry, else_entry);

  EffectVisitor for_then(GetOwner());
  if (!for_then(expr->GetThen()))
    return VisitResult::kStop;
  AppendFragment(then_entry, for_then);

  if (expr->HasElse()) {
    EffectVisitor for_else(GetOwner());
    if (!for_else(expr->GetElse()))
      return VisitResult::kStop;
    AppendFragment(else_entry, for_else);
  }

  SetExit(join);
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

auto EffectVisitor::VisitAssignment(expr::AssignmentExpr* expr) -> VisitResult {
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

auto ValueVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  ValueList args{};
  for (const auto arg : expr->GetArgs()) {
    ValueVisitor for_arg(GetOwner());
    if (!for_arg(arg))
      return VisitResult::kStop;
    Append(for_arg);
    args.push_back(for_arg.GetValue());
  }

  ValueVisitor for_target(GetOwner());
  if (!for_target(expr->GetTarget()))
    return VisitResult::kStop;
  Append(for_target);

  ReturnDefinition(CallInstr::New(for_target.GetValue(), std::move(args)));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  ReturnDefinition(ConstantInstr::New(expr->GetValue()));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  ValueVisitor for_value(GetOwner());
  if (!expr->GetValue()->Accept(&for_value))
    return VisitResult::kStop;
  Append(for_value);
  ReturnDefinition(UnaryOpInstr::New(expr->GetOp(), for_value.GetValue()));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  ValueVisitor for_left(GetOwner());
  if (!expr->GetLeft()->Accept(&for_left))
    return VisitResult::kStop;
  Append(for_left);

  ValueVisitor for_right(GetOwner());
  if (!expr->GetRight()->Accept(&for_right))
    return VisitResult::kStop;
  Append(for_right);

  ReturnDefinition(BinaryOpInstr::New(expr->GetOp(), for_left.GetValue(), for_right.GetValue()));
  return VisitResult::kContinue;
}

auto ValueVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  const auto record = Bind(NewRecordInstr::New());
  for (const auto& prop : expr->GetProperties()) {
    const auto name = prop->GetPropertyName();

    const auto value = prop->GetValue();
    ValueVisitor for_value(GetOwner());
    if (!value->Accept(&for_value))
      return VisitResult::kStop;
    Append(for_value);

    const auto property = Property::New(String::New(std::move(name)));  // TODO(@s0cks): get property at parse time?
    AddStoreProperty(record, property, for_value.GetValue());
    // // TODO(@s0cks): is add the correct way to process StorePropertyInstr since it is not a Definition?
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
  for (auto idx = 0; idx < expr->GetNumberOfChildren(); idx++) {
    const auto value = expr->GetChildAt(idx);
    if (!value)
      return VisitResult::kStop;

    ValueVisitor for_value(GetOwner());
    if (!for_value(value))
      return VisitResult::kStop;
    Append(for_value);
    values.push_back(for_value.GetValue());
  }

  ReturnDefinition(NewListInstr::New(std::move(values)));
  return VisitResult::kContinue;
}
}  // namespace kura
