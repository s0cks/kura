#include "type.h"

#include <sstream>

#include "object.h"
#include "property.h"
#include "record.h"
#include "type_id.h"

namespace kura {
struct XXHash64StringView {
  using is_transparent = void;

  auto operator()(const std::string_view k) const noexcept -> size_t {
    return static_cast<size_t>(XXH64(k.data(), k.size(), 0));
  }
};

static std::vector<Type*> types_{};
static TypeId current_id_ = 0;
static std::unordered_map<std::string, TypeId, XXHash64StringView, std::equal_to<>> str2typeid_{};

void Type::Init() {
  Bool::Init();
  None::Init();
  String::Init();
  Number::Init();
  Seq::Init();
  Property::Init();
  Record::Init();
}

auto Type::AllocTypeId() -> TypeId {
  return current_id_++;
}

auto Type::Get(const TypeId id) -> Type* {
  return types_.at(static_cast<uint64_t>(id));
}

auto Type::Find(const std::string name) -> Type* {
  const auto pos = str2typeid_.find(name);
  return pos != std::end(str2typeid_) ? Get(pos->second) : nullptr;
}

auto Type::Find(String* name) -> Type* {
  return Find(name->GetValue());
}

auto Type::New(String* name) -> Type* {
  const auto id = current_id_++;
  return new Type(id, name);
}

auto Type::New(const std::string name) -> Type* {
  return New(String::New(name));
}

auto Type::FindOrCreate(String* name) -> Type* {
  Type* result = nullptr;
  if ((result = Find(name)))
    return result;
  return result = New(name);
}

auto Type::FindOrCreate(const std::string name) -> Type* {
  Type* result = nullptr;
  if ((result = Find(name)))
    return result;
  return result = New(std::move(name));
}

auto Type::Equals(const Type& rhs) const -> bool {
  return GetId() == rhs.GetId();
}

auto Type::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Type(";
  ss << "id=" << static_cast<uint64_t>(GetId()) << ", ";
  ss << "name=" << GetName()->ToString();
  ss << ")";
  return ss.str();
}
}  // namespace kura
