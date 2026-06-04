#include "function.h"

#include <sstream>

namespace kura {
auto Function::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Function(";
  ss << "name=" << name_->GetValue();
  if (HasBody())
    ss << ", body=" << ((void*)body_);
  if (HasEntry())
    ss << ", entry=" << ((void*)entry_);
  ss << ")";
  return ss.str();
}
}  // namespace kura
