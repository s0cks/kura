#include "local_scope.h"

#include <sstream>

namespace kura {
auto LocalVariable::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LocalVariable(";
  ss << "owner=" << GetOwner() << ", ";
  ss << "name=" << GetName();
  ss << ")";
  return ss.str();
}

auto LocalScope::GetLocal(const std::string name) const -> LocalVariable* {
  const auto pos = locals_.find(name);
  return pos != std::end(locals_) ? pos->second : nullptr;
}

auto LocalScope::GetLocalRecursive(const std::string name) const -> LocalVariable* {
  LocalVariable* result = nullptr;
  auto scope = this;
  while (scope) {
    if ((result = scope->GetLocal(name)))
      goto finished;
    scope = scope->GetParent();
  }
finished:
  return result;
}

auto LocalScope::CreateLocal(const std::string name) -> LocalVariable* {
  const auto local = LocalVariable::New(std::move(name));
  if (!Insert(local))
    return nullptr;  // TODO(@s0cks): free local
  local->SetOwner(this);
  return local;
}
}  // namespace kura
