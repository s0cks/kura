#include "record.h"

#include <sstream>

namespace kura {
void Record::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
}

auto Record::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Record(";
  if (!properties_.empty()) {
    ss << "properties=[";
    uint64_t idx = 0;
    for (const auto& property : properties_) {
      ss << property.first << "=" << property.second->ToString();
      if (++idx < properties_.size() - 1)
        ss << ", ";
    }
    ss << "]";
  }
  ss << ")";
  return ss.str();
}

auto Record::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  for (const auto& property : properties_) {
    if (!vis->Visit(property.second))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}
}  // namespace kura
