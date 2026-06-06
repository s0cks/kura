#ifndef KURA_PROPERTY_H
#define KURA_PROPERTY_H

#include "object.h"

namespace kura {
using PropertyId = uint64_t;

#define FOR_EACH_BUILTIN_PROPERTY(V) \
  V(XX)                              \
  V(XXX)

// clang-format off
enum BuiltinPropertyIds : PropertyId {
  kInvalidPropertyId = 0,
#define DEFINE_BUILTIN_PROPERTY_ID(Name) \
  k##Name##PropertyId,
  FOR_EACH_BUILTIN_PROPERTY(DEFINE_BUILTIN_PROPERTY_ID)
#undef DEFINE_BUILTIN_PROPERTY_ID
  kTotalNumberOfBuiltinProperties,
};
// clang-format on

class Property;
using PropertyVisitor = TemplateVisitor<Property>;
using PropertyPointerVisitor = TemplatePointerVisitor<Property>;

class Property : public TemplateObject<kPropertyType> {
 private:
  PropertyId id_;
  String* name_;
  Type* type_;

  Property(PropertyId id, String* name, Type* type) :
    id_(id),
    name_(name),
    type_(type) {}

 public:
  ~Property() override = default;

  auto GetPropertyId() const -> PropertyId {
    return id_;
  }

  auto GetPropertyName() const -> String* {
    return name_;
  }

  auto GetPropertyType() const -> Type* {
    return type_;
  }

  auto VisitChildren(ObjectVisitor* vis) -> VisitResult override;
  DECLARE_TYPE(Property);

 private:
  static auto New(String* name, Type* type) -> Property*;

 public:
  static auto Get(const PropertyId id) -> Property*;
  static auto FindPropertyId(String* name, Type* type) -> PropertyId;

  static inline auto Find(String* name, Type* type) -> Property* {
    return Get(FindPropertyId(name, type));
  }

  static inline auto FindOrCreate(String* name, Type* type) -> Property* {
    const auto prop = Find(name, type);
    if (prop)
      return prop;
    return New(name, type);
  }
};
}  // namespace kura

#endif  // KURA_PROPERTY_H
