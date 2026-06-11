#include "property.h"

#include <sstream>

namespace kura {
struct XXStringHash {
  using is_transparent = void;

  auto operator()(const String* k) const noexcept -> size_t {
    const auto& data = k->GetData();
    return static_cast<size_t>(XXH64(data.data(), data.size(), 0));
  }
};

struct StringEqual {
  auto operator()(const String* lhs, const String* rhs) const noexcept -> bool {
    return lhs->GetData() == rhs->GetData();
  }
};

using PropertyList = std::vector<Property*>;
using PropertyIdMap = std::unordered_map<String*, PropertyId, XXStringHash, StringEqual>;

static PropertyId current_id_ = kFirstPropertyId;
static PropertyList properties_{};
static PropertyIdMap str2id_{};

static inline auto AllocNewPropertyId() -> PropertyId {
  return current_id_++;
}

void Property::Init() {
  const auto width = NewNumber("width");
  const auto height = NewNumber("height");
  const auto fill = NewSeq("fill");
}

auto Property::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Property(";
  ss << "id=" << GetPropertyId() << ", ";
  ss << "name=" << GetPropertyName()->ToString();
  ss << ")";
  return ss.str();
}

auto Property::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  if (!vis->Visit(name_))
    return false;

  return true;
}

auto Property::Get(const PropertyId id) -> Property* {
  if (id == kInvalidPropertyId || (id - 1) >= current_id_)
    return nullptr;
  return properties_.at(id - 1);
}

auto Property::FindPropertyId(String* name, Type* type) -> PropertyId {
  const auto pos = str2id_.find(name);
  return pos != std::end(str2id_) ? pos->second : kInvalidPropertyId;
}

auto Property::New(String* name, Type* type) -> Property* {
  const auto id = AllocNewPropertyId();
  const auto prop = new Property(id, name, type);
  properties_.push_back(prop);
  str2id_.insert({name, id});
  return prop;
}

auto Property::New(const std::string name, Type* type) -> Property* {
  return New(String::New(std::move(name)), type);
}

auto Property::VisitAllProperties(const std::function<VisitResult(Property*)> vis) -> VisitResult {
  for (auto& property : properties_) {
    if (!vis(property))
      return false;
  }

  return true;
}

auto Property::VisitAllProperties(PropertyVisitor* vis) -> VisitResult {
  for (auto& property : properties_) {
    if (!vis->Visit(property))
      return false;
  }

  return true;
}

auto Property::VisitAllPropertyPointers(PropertyPointerVisitor* vis) -> VisitResult {
  for (auto& property : properties_) {
    if (!vis->Visit(&property))
      return false;
  }

  return true;
}
}  // namespace kura
