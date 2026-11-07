#ifndef KURA_RECORD_H
#define KURA_RECORD_H

#include "object.h"
#include "property.h"

namespace kura {
class Record : public TemplateObject<kRecordType> {
  using PropertyMap = std::unordered_map<std::string, Property*>;

 private:
  PropertyMap properties_{};

 public:
  explicit Record(const PropertyMap properties) :
    properties_(std::move(properties)) {}
  ~Record() override = default;

  auto GetProperties() const -> const PropertyMap& {
    return properties_;
  }

  auto VisitChildren(ObjectVisitor* vis) -> VisitResult override;
  DECLARE_TYPE(Record);

 public:
  static inline auto New(const PropertyMap properties) -> Record* {
    return new Record(std::move(properties));
  }
};
}  // namespace kura

#endif  // KURA_RECORD_H
