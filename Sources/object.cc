#include "object.h"

#include <sstream>

#include "common.h"

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

void Number::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
}

auto Number::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Number(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

void String::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
}

auto String::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "String(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

auto String::GetHash() const -> String::HashType {
  static constexpr const auto kHashSeed = 0;
  const char* data = GetData().data();
  return static_cast<size_t>(XXH64(data, GetLength(), kHashSeed));
}

void Seq::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
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
}  // namespace kura
