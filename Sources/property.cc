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

static PropertyId current_id_ = 0;
static PropertyList properties_{};
static PropertyIdMap str2id_{};

static inline auto AllocNewPropertyId() -> PropertyId {
  return current_id_++;
}

void Property::Init() {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
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
    return VisitResult::kStop;
  return VisitResult::kContinue;
}

auto Property::Get(const PropertyId id) -> Property* {
  if (id == kInvalidPropertyId || id >= current_id_)
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
}  // namespace kura
