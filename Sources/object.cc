#include "object.h"

#include <sstream>

namespace kura {
static None* kNone = nullptr;

void None::Init() {
  kNone = None::New();
}

auto None::Get() -> None* {
  return kNone;
}

auto None::ToString() const -> std::string {
  return "none";
}

static Bool* kTrue = nullptr;
static Bool* kFalse = nullptr;

void Bool::Init() {
  // TODO(@s0cks): check kTrue and kFalse values before initializing
  kTrue = Bool::New(true);
  kFalse = Bool::New(false);
}

auto Bool::False() -> Bool* {
  return kFalse;
}

auto Bool::True() -> Bool* {
  return kTrue;
}

auto Bool::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Bool(";
  ss << "value=" << GetValue();
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
  if (!IsEmpty()) {
    ss << "values=[";
    for (auto idx = 0; idx < GetLength(); idx++) {
      const auto value = GetValueAt(idx);
      if (!value)
        continue;
      ss << value->ToString();
      if (idx < (GetLength() - 1))
        ss << ", ";
    }
    ss << "]";
  }
  ss << ")";
  return ss.str();
}

auto Seq::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  for (const auto& value : values_) {
    if (!vis->Visit(value))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
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

auto Property::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Property(";
  ss << "name=" << GetPropertyName()->ToString();
  if (HasPropertyValue())
    ss << ", value=" << GetPropertyValue()->ToString();
  ss << ")";
  return ss.str();
}

auto Property::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  if (!vis->Visit(name_))
    return VisitResult::kStop;
  if (!vis->Visit(value_))
    return VisitResult::kStop;
  return VisitResult::kContinue;
}
}  // namespace kura
