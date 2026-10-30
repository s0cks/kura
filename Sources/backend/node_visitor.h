#ifndef KURA_NODE_VISITOR_H
#define KURA_NODE_VISITOR_H

#include "frontend/expr.h"
#include "value_visitor.h"

namespace kura {
class NodeVisitor : public ValueVisitor {
  friend class DomLowering;

 private:
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
