#include "dom.h"

#include <cstdlib>
#include <cstring>
#include <print>

namespace kura::dom {
#define DEFINE_ACCEPT(Name)                     \
  auto Name::Accept(NodeVisitor* vis) -> bool { \
    return vis->Visit##Name(this);              \
  }

FOR_EACH_DOM_NODE(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto Container::VisitChildren(NodeVisitor* vis) -> bool {
  for (auto& child : children_) {
    if (!child->Accept(vis))
      return false;
  }
  return true;
}

auto Container::VisitChildren(const std::function<bool(Node*)> vis) -> bool {
  for (auto& child : children_) {
    if (!vis(child))
      return false;
  }
  return true;
}

auto DOMPrinter::VisitDocument(Document* node) -> bool {
  std::println("Document");
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto DOMPrinter::VisitFragment(Fragment* node) -> bool {
  std::println("Fragment");
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

#define DEFINE_VISIT(Name)                         \
  auto DOMPrinter::Visit##Name(Name* node)->bool { \
    PrintNode(node->GetName(), node->node());      \
    {                                              \
      IncrementIndent();                           \
      if (!node->VisitChildren(this))              \
        return false;                              \
      DecrementIndent();                           \
    }                                              \
    return true;                                   \
  }

DEFINE_VISIT(Image);
DEFINE_VISIT(Button);
DEFINE_VISIT(List);
DEFINE_VISIT(Scroll);
DEFINE_VISIT(Viewport);
DEFINE_VISIT(Canvas);
DEFINE_VISIT(Input);
DEFINE_VISIT(Box);
DEFINE_VISIT(Text);
#undef DEFINE_VISIT
}  // namespace kura::dom
