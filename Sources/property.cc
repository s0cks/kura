#include "property.h"

#include <sstream>

namespace kura {
void Property::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
}

auto Property::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Property(";
  ss << "id=" << GetPropertyId() << ", ";
  ss << "name=" << GetPropertyName()->ToString();
  ss << ")";
  return ss.str();
}

auto Property::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  if (!vis->Visit(name_))
    return VisitResult::kStop;
  return VisitResult::kContinue;
}

auto Property::New(PropertyId id, const std::string name) -> Property* {
  return New(id, String::New(name));
}
}  // namespace kura
