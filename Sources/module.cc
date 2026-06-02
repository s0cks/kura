#include "module.h"

#include <sstream>

namespace kura {
auto Module::VisitFunctions(const std::function<bool(Function*)> vis) -> bool {
  for (const auto func : functions) {
    if (!vis(func))
      return false;
  }
  return true;
}

auto Module::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Module(";
  ss << "name=" << name;
  if (!functions.empty()) {
    ss << ", functions=[";
    for (auto idx = 0; idx < functions.size(); idx++) {
      ss << functions[idx]->ToString();
      if (idx < functions.size() - 1)
        ss << ",";
    }
    ss << "]";
  }
  ss << ")";
  return ss.str();
}
}  // namespace kura
