#ifndef KURA_DOM_H
#define KURA_DOM_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <yoga/Yoga.h>

#include "common.h"

namespace kura::dom {
#define FOR_EACH_DOM_PROPERTY(V) \
  V(Width)                       \
  V(Height)

struct Property;
// clang-format off
#define DECLARE_PROPERTY(Name) \
  struct Name##Property;
FOR_EACH_DOM_PROPERTY(DECLARE_PROPERTY)
#undef DECLARE_PROPERTY

enum PropertyType : uint64_t {
  kUnknownProperty = 0,
#define DEFINE_PROPERTY_TYPE(Name) k##Name##Property,
  FOR_EACH_DOM_PROPERTY(DEFINE_PROPERTY_TYPE)
#undef DEFINE_PROPERTY_TYPE
  kTotalNumberOfPropertyTypes,
};
// clang-format on

class Property {
  DEFINE_NON_COPYABLE_TYPE(Property);

 private:
  PropertyType type_;
  Property* next_ = nullptr;

 protected:
  explicit Property(const PropertyType type) :
    type_(type) {}

 public:
  ~Property() = default;

  auto GetPropertyType() const -> PropertyType {
    return type_;
  }

  auto GetNext() const -> Property* {
    return next_;
  }

  inline auto HasNext() const -> bool {
    return next_ != nullptr;
  }

  void SetNext(Property* rhs) {
    next_ = rhs;
  }

 public:
  static inline void Append(Property** props, Property* value) {
    const auto head = (*props);
    if (!head) {
      (*props) = value;
      return;
    }

    auto current = head;
    while (current->next_ && current->next_->type_ < value->type_)
      current = current->next_;
    current->next_ = value;
  }

  static inline auto Find(Property** props, const PropertyType type) -> Property* {
    auto current = (*props);
    while (current != nullptr && current->type_ <= type) {
      if (current->type_ == type)
        return current;
      current = current->next_;
    }
    return nullptr;
  }

  static inline void Remove(Property** props, const PropertyType type) {
    const auto head = (*props);
    if (head->type_ == type) {
      (*props) = head->next_;
      return;
    }

    auto current = head;
    while (current->next_ && current->next_->type_ != type) {
      if (current->next_->type_ > type)
        return;
      current = current->next_;
    }

    if (current->next_) {
      current->next_ = current->next_->next_;
      return;
    }
  }
};

template <typename T>
class PropertyTemplate : public Property {
  DEFINE_NON_COPYABLE_TYPE(PropertyTemplate<T>);

 private:
  T value_{};

 protected:
  explicit PropertyTemplate(const PropertyType type) :
    Property(type) {}
  PropertyTemplate(const PropertyType type, const T value) :
    Property(type),
    value_(std::move(value)) {}

 public:
  ~PropertyTemplate() = default;

  auto GetValue() const -> const T& {
    return value_;
  }

  void SetValue(const T value) {
    value_ = std::move(value);
  }
};

class WidthProperty : public PropertyTemplate<double> {
 public:
  explicit WidthProperty(const double value = 0.0) :
    PropertyTemplate(kWidthProperty, value) {}
  ~WidthProperty() = default;
};

class HeightProperty : public PropertyTemplate<double> {
 public:
  explicit HeightProperty(const double value = 0.0) :
    PropertyTemplate(kHeightProperty, value) {}
  ~HeightProperty() = default;
};

class PropertyIterator {
 private:
  Property* prop_;

 public:
  explicit PropertyIterator(Property* prop) :
    prop_(prop) {}
  ~PropertyIterator() = default;

  auto HasNext() const -> bool {
    return prop_ != nullptr;
  }

  auto Next() -> Property* {
    auto current = prop_;
    prop_ = current->GetNext();
    return current;
  }
};

#define FOR_EACH_DOM_NODE(V) \
  V(Document)                \
  V(Block)                   \
  V(Line)                    \
  V(List)                    \
  V(Text)

class Node;
// clang-format off
#define DECLARE_DOM_TYPE(Name) \
  class Name;
FOR_EACH_DOM_NODE(DECLARE_DOM_TYPE)
#undef DECLARE_DOM_TYPE
// clang-format on

using NodePtr = std::shared_ptr<Node>;
using NodeList = std::vector<NodePtr>;

class NodeVisitor {
 public:
  NodeVisitor() = default;
  virtual ~NodeVisitor() = default;

#define DEFINE_VISIT_NODE(Name) virtual auto Visit##Name(Name* value) -> bool = 0;
  FOR_EACH_DOM_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
};

class Node {
 private:
  YGNodeRef node_ = nullptr;
  Property* properties_ = nullptr;
  std::vector<Node*> children_{};

 public:
  Node() = default;
  virtual ~Node() = default;

  // TODO(@s0cks): reduce visibility
  inline auto node() const -> const YGNodeRef& {
    return node_;
  }

  // TODO(@s0cks): reduce visibility
  inline auto node() -> YGNodeRef& {
    return node_;
  }

  void AddProperty(Property* value) {
    Property::Append(&properties_, value);
  }

  inline auto HasProperties() const -> bool {
    return properties_ != nullptr;
  }

  auto GetPropertyList() const -> Property* {
    return properties_;
  }

  virtual auto Accept(NodeVisitor* vis) -> bool = 0;
  virtual auto GetName() const -> std::string_view = 0;
  virtual auto VisitChildren(NodeVisitor* vis) -> bool;

  void AddChild(Node* rhs) {
    children_.push_back(rhs);
  }

#define DEFINE_TYPE_CHECK(Name)      \
  inline auto Is##Name() -> bool {   \
    return As##Name() != nullptr;    \
  }                                  \
  virtual auto As##Name() -> Name* { \
    return nullptr;                  \
  }
  FOR_EACH_DOM_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK
};

#define DECLARE_DOM_NODE_TYPE(Name)                   \
 public:                                              \
  auto Accept(NodeVisitor* vis) -> bool override;     \
  auto GetName() const -> std::string_view override { \
    return #Name;                                     \
  }                                                   \
  auto As##Name() -> Name* override {                 \
    return this;                                      \
  }

class Document : public Node {
 private:
  NodeList children_{};

 public:
  Document() = default;
  ~Document() override = default;
  DECLARE_DOM_NODE_TYPE(Document);
};

class Line : public Node {
 public:
  Line() = default;
  ~Line() override = default;
  DECLARE_DOM_NODE_TYPE(Line);
};

class Block : public Node {
 public:
  Block() = default;
  ~Block() override = default;
  DECLARE_DOM_NODE_TYPE(Block);
};

class Text : public Block {
 public:
  Text() = default;
  ~Text() override = default;
  DECLARE_DOM_NODE_TYPE(Text);
};

class List : public Block {
 public:
  List() = default;
  ~List() override = default;
  DECLARE_DOM_NODE_TYPE(List);
};

class DOMPrinter : public NodeVisitor {
 private:
  uint64_t indent_;

  inline void IncrementIndent() {
    indent_ += 1;
  }

  inline void DecrementIndent() {
    indent_ -= 1;
  }

  inline void PrintNode(Node* node) {
    std::string indent(' ', indent_ * 2);
    const auto top = YGNodeLayoutGetWidth(node->node());
    const auto left = YGNodeLayoutGetLeft(node->node());
    const auto width = YGNodeLayoutGetWidth(node->node());
    const auto height = YGNodeLayoutGetHeight(node->node());
    const auto name = node->GetName();
    std::printf("%s%s{ top: %lf, left: %lf, width: %lf, height: %lf }\n", indent.data(), name.data(), top, left, width,
                height);
  }

 public:
  explicit DOMPrinter(const uint64_t indent = 0) :
    NodeVisitor(),
    indent_(indent) {}
  ~DOMPrinter() = default;

  auto GetIndent() const -> uint64_t {
    return indent_;
  }

  // clang-format off
#define DEFINE_VISIT_NODE(Name) \
  auto Visit##Name(Name* value) -> bool override;
  FOR_EACH_DOM_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
  // clang-format on
 public:
  static auto Print(Node* node, const uint64_t indent = 0) -> bool {
    DOMPrinter printer(indent);
    return node->Accept(&printer);
  }
};
}  // namespace kura::dom

#endif  // KURA_DOM_H
