#ifndef KURA_BACKEND_DOM_LOWERING_H
#define KURA_BACKEND_DOM_LOWERING_H

#include <string>
#include <unordered_map>

#include "backend/ir.h"
#include "dom/node.h"

namespace kura {
class NodeVisitor;

class DomLowering : public dom::Visitor {
 private:
  using Resolved = std::unordered_map<const dom::PropertyDef*, Value*>;

  NodeVisitor* host_;

  auto ResolvePropertyByName(dom::Node* node, const std::string name, Resolved& resolved) -> Value*;

  auto Fail(const std::string& message) -> bool;
  void EmitRenderCall(Definition* defn);
  auto ResolveProperties(dom::Node* node, Resolved* resolved) -> bool;

 public:
  explicit DomLowering(NodeVisitor* host) :
    dom::Visitor(),
    host_(host) {}
  ~DomLowering() override = default;

  auto VisitNode(dom::Node* node) -> bool override;
  auto VisitButton(dom::Node* node) -> bool override;
  auto VisitText(dom::Node* node) -> bool override;
  auto VisitDynamic(dom::Node* node) -> bool override;
  auto VisitBind(dom::Node* node) -> bool override;

  inline auto Lower(dom::Node* root) -> bool {
    return root->Accept(this);
  }
};
}  // namespace kura

#endif  // KURA_BACKEND_DOM_LOWERING_H
