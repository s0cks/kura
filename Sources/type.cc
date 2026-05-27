#include "type.h"

namespace kura {
auto Object::VisitAllProperties(PropertyVisitor* vis) -> bool {
  for (auto& prop : properties) {
    if (!vis->VisitProperty(prop.first, prop.second))
      return false;
  }
  return true;
}

auto Object::VisitAllPropertyValues(ValueVisitor* vis) -> bool {
  for (auto& prop : properties) {
    if (!vis->Visit(prop.second))
      return false;
  }
  return true;
}

auto Object::VisitAllPropertyKeys(PropertyKeyVisitor* vis) -> bool {
  for (auto& prop : properties) {
    if (!vis->Visit(prop.first))
      return false;
  }
  return true;
}

auto Object::AddProperty(const std::string name, ValuePtr value) -> bool {
  return properties.insert({name, value}).second;
}

auto Object::GetProperty(const std::string name) const -> ValuePtr {
  const auto pos = properties.find(name);
  return pos != std::end(properties) ? pos->second : nullptr;
}

void Module::AddFunction(Function* func) {
  functions_.insert({func->GetName(), func});
}

auto Module::VisitFunctions(FunctionVisitor* vis) -> bool {
  for (const auto& func : functions_) {
    if (!vis->VisitFunction(func.second))
      return false;
  }
  return true;
}

auto Module::VisitFunctions(const std::function<bool(Function*)>& vis) -> bool {
  for (const auto& func : functions_) {
    if (!vis(func.second))
      return false;
  }
  return true;
}
}  // namespace kura
