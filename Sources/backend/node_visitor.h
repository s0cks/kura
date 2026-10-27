#ifndef KURA_NODE_VISITOR_H
#define KURA_NODE_VISITOR_H

#include "frontend/expr.h"
#include "value_visitor.h"

namespace kura {
// Lowers an expression of a `view`, where the nodes it produces are rendered
// instead of built.
//
// The nodes of an expression are collected into a virtual DOM (see dom::Builder)
// and lowered by DomLowering. What is in between, the `if`s and `for`s that
// choose which nodes there are, is lowered here as the control flow it is, with
// the nodes inside its arms rendered in turn.
class NodeVisitor : public ValueVisitor {
  friend class DomLowering;

 private:
  // Builds the DOM of `expr` and lowers it.
  auto Render(expr::Expr* expr) -> VisitResult;

  auto EvalArm(expr::Expr* expr, Value** value) -> bool override;

 public:
  explicit NodeVisitor(FlowGraphBuilder* owner) :
    ValueVisitor(owner) {}
  ~NodeVisitor() = default;

  auto VisitNode(expr::NodeExpr* expr) -> VisitResult override;
  auto VisitSeq(expr::SeqExpr* expr) -> VisitResult override;
  auto VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult override;
};
}  // namespace kura

#endif  // KURA_NODE_VISITOR_H
