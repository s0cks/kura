#include "function.h"

#include <sstream>

#include "frontend/expr.h"

namespace kura {
auto Function::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Function(";
  ss << "name=" << name_->GetValue();
  if (HasBody())
    ss << ", body=" << GetBody()->GetName();
  if (HasEntry())
    ss << ", entry=" << ((void*)entry_);
  ss << ")";
  return ss.str();
}
}  // namespace kura
