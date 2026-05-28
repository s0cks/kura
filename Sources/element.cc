#include "element.h"

#include <cstdlib>
#include <cstring>
#include <print>

namespace kura::elem {
#define DEFINE_ACCEPT(Name)                     \
  auto Name::Accept(NodeVisitor* vis) -> bool { \
    return vis->Visit##Name(this);              \
  }

FOR_EACH_ELEMENT_NODE(DEFINE_ACCEPT)
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
}  // namespace kura::elem
