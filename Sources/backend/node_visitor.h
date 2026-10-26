#ifndef KURA_NODE_VISITOR_H
#define KURA_NODE_VISITOR_H

#include "frontend/expr.h"
#include "value_visitor.h"

namespace kura {
class NodeVisitor : public ValueVisitor {
 private:
  void EmitRenderCall(Definition* defn);

  auto VisitButton(expr::NodeExpr* node) -> VisitResult;
  auto VisitText(expr::NodeExpr* node) -> VisitResult;

 public:
  explicit NodeVisitor(FlowGraphBuilder* owner) :
    ValueVisitor(owner) {}
  ~NodeVisitor() = default;

  auto VisitNode(expr::NodeExpr* expr) -> VisitResult override;
  auto VisitSeq(expr::SeqExpr* expr) -> VisitResult override;
};
}  // namespace kura

#endif  // KURA_NODE_VISITOR_H
