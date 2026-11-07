#ifndef KURA_PROPERTY_H
#define KURA_PROPERTY_H

#include "object.h"

namespace kura {
using PropertyId = uint64_t;
static constexpr const auto kInvalidPropertyId = 0;
static constexpr const auto kFirstPropertyId = 1;

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

  static auto New(const std::string name, Type* type) -> Property*;

#define DEFINE_NEW(Name)                                              \
  static inline auto New##Name(String* name) -> Property* {           \
    return New(name, Type::Name##Type());                             \
  }                                                                   \
  static inline auto New##Name(const std::string name) -> Property* { \
    return New(std::move(name), Type::Name##Type());                  \
  }

  DEFINE_NEW(Seq);
  DEFINE_NEW(Bool);
  DEFINE_NEW(Number);
  DEFINE_NEW(String);
#undef DEFINE_NEW

 public:
  static auto VisitAllProperties(PropertyVisitor* vis) -> VisitResult;
  static auto VisitAllProperties(const std::function<VisitResult(Property*)> vis) -> VisitResult;
  static auto VisitAllPropertyPointers(PropertyPointerVisitor* vis) -> VisitResult;
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
