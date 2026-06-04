#ifndef KURA_PROPERTY_H
#define KURA_PROPERTY_H

#include "object.h"

namespace kura {
using PropertyId = uint64_t;
class Property : public TemplateObject<kPropertyType> {
 private:
  PropertyId id_;
  String* name_;
  Object* value_;

  Property(PropertyId id, String* name) :
    id_(id),
    name_(name) {}

 public:
  ~Property() override = default;

  auto GetPropertyId() const -> PropertyId {
    return id_;
  }

  auto GetPropertyName() const -> String* {
    return name_;
  }

  auto VisitChildren(ObjectVisitor* vis) -> VisitResult override;
  DECLARE_TYPE(Property);

 public:
  static auto New(PropertyId id, const std::string name) -> Property*;

  static inline auto New(PropertyId id, String* name) -> Property* {
    return new Property(id, name);
  }
};
}  // namespace kura

#endif  // KURA_PROPERTY_H
