#include "backend/flow_graph_builder.h"

#include <iostream>
#include <sstream>

#include "backend/effect_visitor.h"
#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "backend/value_visitor.h"
#include "common.h"
#include "frontend/expr.h"

namespace kura {
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

  if (for_expr.IsOpen()) {
    for_expr.AddLabel("default_generated_return");

    Value* value = for_expr.GetValue();
    if (!value)
      value = for_expr.Bind(ConstantInstr::New(None::Get()));

    for_expr.AddReturnExit(value);
  }

  return VisitResult::kContinue;
}

auto FlowGraphBuilder::BuildFlowGraph(expr::SeqExpr* expr) -> FlowGraph* {
  if (!expr)
    return nullptr;
  FlowGraphBuilder builder{};
  const auto result = builder(expr);
  return result ? builder.Build() : nullptr;
}

}  // namespace kura
