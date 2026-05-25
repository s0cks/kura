#include "dom.h"

#include <cstdlib>
#include <cstring>

namespace kura::dom {
#define DEFINE_ACCEPT(Name)                     \
  auto Name::Accept(NodeVisitor* vis) -> bool { \
    return vis->Visit##Name(this);              \
  }

FOR_EACH_DOM_NODE(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto Node::VisitChildren(NodeVisitor* vis) -> bool {
  for (auto& child : children_) {
    if (!child->Accept(vis))
      return false;
  }
  return true;
}

auto DOMPrinter::VisitDocument(Document* node) -> bool {
  PrintNode(node);
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto DOMPrinter::VisitLine(Line* node) -> bool {
  PrintNode(node);
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto DOMPrinter::VisitList(List* node) -> bool {
  PrintNode(node);
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto DOMPrinter::VisitText(Text* node) -> bool {
  PrintNode(node);
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto DOMPrinter::VisitBlock(Block* node) -> bool {
  PrintNode(node);
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}
}  // namespace kura::dom
