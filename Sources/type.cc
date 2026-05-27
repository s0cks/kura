#include "type.h"

#include <sstream>

namespace kura {
auto Bool::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Bool(";
  ss << "value=" << (GetValue() ? "true" : "false");
  ss << ")";
  return ss.str();
}

auto Number::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Number(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

auto String::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "String(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

auto Seq::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Seq(";
  ss << ")";
  return ss.str();
}

auto Record::ToString() const -> std::string {
  return {};
}

auto UnionType::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Union(";
  ss << "name=" << GetName();
  ss << ")";
  return ss.str();
}

auto Function::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Function(";
  ss << "name=" << GetName();
  ss << ")";
  return ss.str();
}

auto Module::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Module(";
  ss << "name=" << GetName();
  ss << ")";
  return ss.str();
}
}  // namespace kura
