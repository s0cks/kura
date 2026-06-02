#include "function.h"

#include <sstream>

namespace kura {
auto Function::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Function(";
  ss << "name=" << name;
  if (HasBody())
    ss << ", body=" << ((void*)body);
  if (HasEntry())
    ss << ", entry=" << ((void*)entry);
  ss << ")";
  return ss.str();
}
}  // namespace kura
