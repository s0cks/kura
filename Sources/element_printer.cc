#include "element_printer.h"

#include <print>

namespace kura::elem {
auto ElementPrinter::VisitDocument(Document* node) -> bool {
  std::println("Document");
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

auto ElementPrinter::VisitFragment(Fragment* node) -> bool {
  std::println("Fragment");
  {
    IncrementIndent();
    if (!node->VisitChildren(this))
      return false;
    DecrementIndent();
  }
  return true;
}

#define DEFINE_VISIT(Name)                             \
  auto ElementPrinter::Visit##Name(Name* node)->bool { \
    PrintNode(node->GetName(), node->node());          \
    {                                                  \
      IncrementIndent();                               \
      if (!node->VisitChildren(this))                  \
        return false;                                  \
      DecrementIndent();                               \
    }                                                  \
    return true;                                       \
  }

DEFINE_VISIT(Image);
DEFINE_VISIT(Button);
DEFINE_VISIT(List);
DEFINE_VISIT(Scroll);
DEFINE_VISIT(Viewport);
DEFINE_VISIT(Canvas);
DEFINE_VISIT(Input);
DEFINE_VISIT(Block);
DEFINE_VISIT(Text);
#undef DEFINE_VISIT
}  // namespace kura::elem
