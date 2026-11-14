#ifndef KURA_BACKEND_DOM_LOWERING_H
#define KURA_BACKEND_DOM_LOWERING_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir.h"
#include "backend/layout_plan.h"
#include "dom/node.h"

namespace kura {
class NodeVisitor;

// Lowers a DOM tree to the calls that draw it.
//
// It takes two passes over the tree, because where a node is drawn depends on every node's size:
//
//  1. Collect: in tree order, evaluate every property and bind, and tell the layout the content of each
//     text. Nothing is drawn yet; what's to be drawn is noted down.
//  2. Draw: lay the tree out, then emit the draws in the order noted, reading each box from the layout.
//     Draws keep the tree's order, so a parent is painted before its children.
//
// A dynamic node (`for`, `if`, `match`, a helper that returns nodes) is a slot in the tree's layout, and what it
// makes are instances of template plans under the slot, so they are laid out with the tree around them. That
// means the hole has to be run twice, because its instances must exist before the layout and be drawn after it:
//
//  * in the first pass it is run to make its instances and say what they hold (what the Collect pass does for
//    the other nodes);
//  * in the second pass it is run again, to draw them, at its place in the draw order. The view is pure, so
//    what it works out the second time is what it did the first.
//
// A tree that is itself what a hole makes is lowered the same way, except that it is never laid out on its own:
// the tree it is under does that, once, for all of them.
class DomLowering : public dom::Visitor {
 private:
  using Resolved = std::unordered_map<const dom::PropertyDef*, Value*>;

  struct Op {
    dom::Node* node = nullptr;
    Value* text = nullptr;  // a text's content
    // the mark after what is inside a scroll container, where its clip ends
    bool end = false;
  };

  NodeVisitor* host_;
  std::shared_ptr<LayoutPlan> plan_{};
  // when this tree is what a hole makes, and that hole is being drawn: its layout is set already
  bool drawing_hole_ = false;
  std::unordered_map<const dom::Node*, Resolved> resolved_{};
  std::vector<Op> ops_{};

  auto ResolvePropertyByName(dom::Node* node, const std::string name, Resolved& resolved) -> Value*;

  auto Fail(const std::string& message) -> bool;
  void EmitRenderCall(Definition* defn);
  auto ResolveProperties(dom::Node* node, Resolved* resolved) -> bool;
  void EmitLayoutProperties(dom::Node* node, const Resolved& resolved);
  auto Box(dom::Node* node, int component) -> Value*;
  void EmitEvents(dom::Node* node);
  auto DrawButton(dom::Node* node) -> bool;
  auto DrawBlock(dom::Node* node) -> bool;
  auto DrawView(dom::Node* node) -> bool;
  auto DrawScroll(const Op& op) -> bool;
  auto DrawText(const Op& op) -> bool;
  auto RunHole(dom::Node* node, bool draw) -> bool;

 public:
  explicit DomLowering(NodeVisitor* host) :
    dom::Visitor(),
    host_(host) {}
  ~DomLowering() override = default;

  auto VisitNode(dom::Node* node) -> bool override;
  auto VisitButton(dom::Node* node) -> bool override;
  auto VisitBlock(dom::Node* node) -> bool override;
  auto VisitView(dom::Node* node) -> bool override;
  auto VisitScroll(dom::Node* node) -> bool override;
  auto VisitList(dom::Node* node) -> bool override;
  auto VisitScrollable(dom::Node* node) -> bool;
  auto VisitText(dom::Node* node) -> bool override;
  auto VisitDynamic(dom::Node* node) -> bool override;
  auto VisitBind(dom::Node* node) -> bool override;

  auto Lower(dom::Node* root) -> bool;
};
}  // namespace kura

#endif  // KURA_BACKEND_DOM_LOWERING_H
