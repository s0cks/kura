#include "element_property.h"

namespace kura::elem {
auto Property::Accept(PropertyVisitor* vis) -> bool {
  switch (id) {
#define DEFINE_CASE(Name) \
  case k##Name##Property: \
    return vis->Visit##Name(this);
    FOR_EACH_ELEMENT_PROPERTY(DEFINE_CASE)
#undef DEFINE_CASE
    case kUnknownProperty:
    default:
      return false;
  }
}
}  // namespace kura::elem
