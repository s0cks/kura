#include "local_scope.h"

#include <sstream>

#include "object.h"

namespace kura {
auto LocalVariable::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LocalVariable(";
  ss << "owner=" << GetOwner() << ", ";
  ss << "name=" << GetName();
  ss << ")";
  return ss.str();
}

auto LocalVariable::New(const std::string name) -> LocalVariable* {
  return New(String::New(std::move(name)));
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

auto LocalScope::CreateLocal(String* name) -> LocalVariable* {
  const auto local = LocalVariable::New(name);
  if (!Insert(local))
    return nullptr;  // TODO(@s0cks): free local
  local->SetOwner(this);
  return local;
}

auto LocalScope::CreateLocal(const std::string name) -> LocalVariable* {
  return CreateLocal(String::New(std::move(name)));
}

auto LocalScope::Insert(LocalVariable* local) -> bool {
  const auto pos = locals_.insert({local->GetName()->GetValue(), local});
  return pos.second;
}
}  // namespace kura
