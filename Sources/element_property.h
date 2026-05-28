#ifndef KURA_ELEMENT_PROPERTY_H
#define KURA_ELEMENT_PROPERTY_H

#include <cstdint>
#include <string>
#include <variant>

namespace kura::elem {
#define FOR_EACH_ELEMENT_PROPERTY(V) \
  V(Width)                           \
  V(Height)

struct Property;
class PropertyVisitor;
// clang-format off
enum PropertyId : uint64_t {
  kUnknownProperty = 0,
#define DEFINE_PROPERTY_TYPE(Name) k##Name##Property,
  FOR_EACH_ELEMENT_PROPERTY(DEFINE_PROPERTY_TYPE)
#undef DEFINE_PROPERTY_TYPE
  kTotalNumberOfPropertyTypes,
};
// clang-format on

struct Property {
  PropertyId id;
  Property* next = nullptr;
  std::variant<bool, double, std::string> value;

  inline auto HasNext() const -> bool {
    return next != nullptr;
  }

#define DEFINE_TYPE_CHECK(Name)          \
  inline auto Is##Name() const -> bool { \
    return id == k##Name##Property;      \
  }
  FOR_EACH_ELEMENT_PROPERTY(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  auto Accept(PropertyVisitor* vis) -> bool;

 public:
  static inline void Append(Property** list, Property* value) {
    if (list == nullptr || value == nullptr)
      return;

    if (*list == nullptr) {
      *list = value;
      return;
    }

    if (*list == value)
      return;

    auto current = *list;
    while (current->next != nullptr)
      current = current->next;
    current->next = value;
  }

  static inline auto InsertAfter(Property* previous, Property* prop) -> bool {
    if (previous == nullptr || prop == nullptr)
      return false;

    prop->next = previous->next;
    previous->next = prop;
    return true;
  }

  static inline auto InsertBefore(Property* next, Property* prop) -> bool {
    if (next == nullptr || prop == nullptr)
      return false;

    prop->next = next->next;
    next->next = prop;
    std::swap(next->id, prop->id);
    std::swap(next->value, prop->value);
    return true;
  }
};

class PropertyVisitor {
 protected:
  PropertyVisitor() = default;

 public:
  virtual ~PropertyVisitor() = default;
#define DEFINE_VISIT(Name) virtual auto Visit##Name(Property* prop) -> bool = 0;
  FOR_EACH_ELEMENT_PROPERTY(DEFINE_VISIT)
#undef DEFINE_VISIT
};

class PropertyIterator {
 private:
  Property* current_;

 public:
  explicit PropertyIterator(Property* prop) :
    current_(prop) {}
  ~PropertyIterator() = default;

  auto HasNext() const -> bool {
    return current_ != nullptr;
  }

  auto Next() -> Property* {
    auto current = current_;
    current_ = current->next;
    return current;
  }

  auto operator*() const -> Property* {
    return current_;
  }

  auto operator++() -> PropertyIterator& {
    if (current_)
      current_ = current_->next;
    return *this;
  }

  auto operator!=(const PropertyIterator& other) const -> bool {
    return current_ != other.current_;
  }
};

class PropertyRange {
 private:
  Property* head_;

 public:
  explicit PropertyRange(Property* head) :
    head_(head) {}

  auto begin() const -> PropertyIterator {
    return PropertyIterator(head_);
  }

  auto end() const -> PropertyIterator {
    return PropertyIterator(nullptr);
  }
};
}  // namespace kura::elem

#endif  // KURA_ELEMENT_PROPERTY_H
