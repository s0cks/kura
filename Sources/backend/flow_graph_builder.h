#ifndef KURA_FLOW_GRAPH_BUILDER_H
#define KURA_FLOW_GRAPH_BUILDER_H

#include "backend/ir.h"
#include "common.h"
#include "frontend/expr.h"

namespace kura {
template <class I>
concept HasBlockId = requires(I instr, const uint64_t id) {
  { instr.SetBlockId(id) } -> std::same_as<void>;
  { instr.GetBlockId() } -> std::same_as<BlockId>;
};

class FlowGraph;
class FlowGraphBuilder {
  friend class EffectVisitor;
  friend class ValueVisitor;

 private:
  BlockId current_block_id_ = 0;
  TargetEntryInstr* target_;
  GraphEntryInstr* graph_;

  template <HasBlockId I = TargetEntryInstr>
  inline auto CreateNewBlock() -> I* {
    const auto entry = I::New();
    entry->SetBlockId(current_block_id_++);
    return entry;
  }

 public:
  FlowGraphBuilder();
  ~FlowGraphBuilder() = default;

  auto Visit(expr::SeqExpr* expr) -> VisitResult;
  auto Build() -> FlowGraph*;

  inline auto operator()(expr::SeqExpr* expr) -> VisitResult {
    return Visit(expr);
  }

 public:
  static auto BuildFlowGraph(expr::SeqExpr* expr) -> FlowGraph*;
};

#define _CREATE_FRAGMENT(Visitor, Name, Expression) \
  Visitor for_##Name(GetOwner());                   \
  expr::Expr* Name##_expr = (Expression);           \
  if (Name##_expr) {                                \
    if (!for_##Name(Name##_expr))                   \
      return false;                                 \
  }

#define CREATE_EFFECT_FRAGMENT(Name, Expr) _CREATE_FRAGMENT(EffectVisitor, Name, Expr)
#define CREATE_VALUE_FRAGMENT(Name, Expr)  _CREATE_FRAGMENT(ValueVisitor, Name, Expr)

#define _CREATE_TARGET_FRAGMENT(Visitor, Name, Expression) \
  TargetEntryInstr* Name##_entry = nullptr;                \
  Visitor for_##Name(GetOwner());                          \
  expr::Expr* Name##_expr = (Expression);                  \
  if (Name##_expr) {                                       \
    Name##_entry = GetOwner()->CreateNewBlock();           \
    if (!for_##Name(Name##_expr))                          \
      return false;                                        \
    AppendFragment(Name##_entry, for_##Name);              \
  }

#define CREATE_TARGET_FRAGMENT(Name, Expression)       _CREATE_TARGET_FRAGMENT(EffectVisitor, Name, Expression)

#define CREATE_TARGET_VALUE_FRAGMENT(Name, Expression) _CREATE_TARGET_FRAGMENT(ValueVisitor, Name, Expression)

}  // namespace kura

#endif  // KURA_FLOW_GRAPH_BUILDER_H
