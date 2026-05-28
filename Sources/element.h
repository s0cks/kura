#ifndef KURA_ELEMENT_H
#define KURA_ELEMENT_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <yoga/Yoga.h>

#include "common.h"

namespace kura::elem {
#define FOR_EACH_ELEMENT_PROPERTY(V) \
  V(Width)                           \
  V(Height)

class Property;
// clang-format off
#define DECLARE_PROPERTY(Name) \
  class Name##Property;
FOR_EACH_ELEMENT_PROPERTY(DECLARE_PROPERTY)
#undef DECLARE_PROPERTY

enum PropertyType : uint64_t {
  kUnknownProperty = 0,
#define DEFINE_PROPERTY_TYPE(Name) k##Name##Property,
  FOR_EACH_ELEMENT_PROPERTY(DEFINE_PROPERTY_TYPE)
#undef DEFINE_PROPERTY_TYPE
  kTotalNumberOfPropertyTypes,
};
// clang-format on

class PropertyVisitor {
 protected:
  PropertyVisitor() = default;

 public:
  virtual ~PropertyVisitor() = default;
  virtual auto VisitProperty(Property* prop) -> bool = 0;
};

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

  auto Accept(PropertyVisitor* vis) -> bool {
    return vis->VisitProperty(this);
  }

  auto VisitNextProperty(PropertyVisitor* vis) -> bool {
    return next_ ? next_->Accept(vis) : true;
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

#define FOR_EACH_BASIC_ELEMENT_PROPERTY(V) \
  V(Width, double, 0.0)                    \
  V(Height, double, 0.0)

#define DEFINE_BASIC_ELEMENT_PROPERTY(Name, Type, InitValue) \
  class Name##Property : public PropertyTemplate<Type> {     \
   public:                                                   \
    explicit Name##Property(const Type value = InitValue) :  \
      PropertyTemplate(k##Name##Property, value) {}          \
    ~Name##Property() = default;                             \
  };

FOR_EACH_BASIC_ELEMENT_PROPERTY(DEFINE_BASIC_ELEMENT_PROPERTY)
#undef DEFINE_BASIC_ELEMENT_PROPERTY

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

#define FOR_EACH_ELEMENT_NODE(V) \
  V(Document)                    \
  V(Fragment)                    \
  V(Box)                         \
  V(Button)                      \
  V(List)                        \
  V(Text)                        \
  V(Image)                       \
  V(Viewport)                    \
  V(Canvas)                      \
  V(Input)                       \
  V(Scroll)

class Node;
// clang-format off
#define DECLARE_ELEMENT_TYPE(Name) \
  class Name;
FOR_EACH_ELEMENT_NODE(DECLARE_ELEMENT_TYPE)
#undef DECLARE_ELEMENT_TYPE
// clang-format on

using NodeList = std::vector<Node*>;

class NodeVisitor {
 public:
  NodeVisitor() = default;
  virtual ~NodeVisitor() = default;

#define DEFINE_VISIT_NODE(Name) virtual auto Visit##Name(Name* value) -> bool = 0;
  FOR_EACH_ELEMENT_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
};

class Node {
  DEFINE_NON_COPYABLE_TYPE(Node);

 protected:
  Node() = default;

 public:
  virtual ~Node() = default;
  virtual auto Accept(NodeVisitor* vis) -> bool = 0;
  virtual auto GetName() const -> std::string_view = 0;
  virtual auto VisitChildren(NodeVisitor* vis) -> bool = 0;
  virtual auto VisitChildren(std::function<bool(Node*)> vis) -> bool = 0;
};

class Container : public Node {
 private:
  NodeList children_{};

 protected:
  explicit Container(const NodeList children = {}) :
    Node(),
    children_(children) {}

  void AddChild(Node* node) {
    children_.push_back(node);
  }

 public:
  ~Container() override = default;

  auto GetChildren() const -> const NodeList& {
    return children_;
  }

  auto GetNumberOfChildren() const -> size_t {
    return children_.size();
  }

  auto HasChildren() const -> bool {
    return !children_.empty();
  }

  auto GetChildAt(const size_t idx) const -> Node* {
    return children_[idx];
  }

  auto VisitChildren(NodeVisitor* vis) -> bool override;
  auto VisitChildren(std::function<bool(Node*)> vis) -> bool override;
};

class Box : public Container {
 private:
  YGNodeRef node_ = nullptr;
  Property* properties_ = nullptr;

 public:
  Box() = default;
  ~Box() override = default;

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

  auto Accept(NodeVisitor* vis) -> bool override;
#define DEFINE_TYPE_CHECK(Name)      \
  inline auto Is##Name() -> bool {   \
    return As##Name() != nullptr;    \
  }                                  \
  virtual auto As##Name() -> Name* { \
    return nullptr;                  \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK
};

#define DECLARE_ELEMENT_NODE_TYPE(Name)               \
  DEFINE_NON_COPYABLE_TYPE(Name);                     \
                                                      \
 public:                                              \
  auto Accept(NodeVisitor* vis) -> bool override;     \
  auto GetName() const -> std::string_view override { \
    return #Name;                                     \
  }

class Document : public Container {
 public:
  explicit Document(const NodeList children) :
    Container(std::move(children)) {}
  ~Document() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Document);

 public:
  static inline auto New(const NodeList children = {}) -> Document* {
    return new Document(children);
  }
};

class Fragment : public Container {
 public:
  explicit Fragment(const NodeList children) :
    Container(std::move(children)) {}
  ~Fragment() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Fragment);

 public:
  static inline auto New(const NodeList children = {}) -> Fragment* {
    return new Fragment(children);
  }
};

class Button : public Box {
 public:
  Button() = default;
  ~Button() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Button);

 public:
  static inline auto New() -> Button* {
    return new Button();
  }
};

class Text : public Box {
 private:
  std::string value_;

 public:
  Text(const std::string value) :
    Box(),
    value_(std::move(value)) {}
  ~Text() override = default;

  auto GetValue() const -> const std::string& {
    return value_;
  }

  DECLARE_ELEMENT_NODE_TYPE(Text);

 public:
  static inline auto New(const std::string value) -> Text* {
    return new Text(std::move(value));
  }
};

class Image : public Box {
 public:
  Image() = default;
  ~Image() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Image);

 public:
  static inline auto New() -> Image* {
    return new Image();
  }
};

class Canvas : public Box {
 public:
  Canvas() = default;
  ~Canvas() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Canvas);

 public:
  static inline auto New() -> Canvas* {
    return new Canvas();
  }
};

class Input : public Box {
 public:
  Input() = default;
  ~Input() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Input);

 public:
  static inline auto New() -> Input* {
    return new Input();
  }
};

class Scroll : public Box {
 public:
  Scroll() = default;
  ~Scroll() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Scroll);

 public:
  static inline auto New() -> Scroll* {
    return new Scroll();
  }
};

class List : public Box {
 public:
  List() = default;
  ~List() override = default;

  DECLARE_ELEMENT_NODE_TYPE(List);

 public:
  static inline auto New() -> List* {
    return new List();
  }
};

class Viewport : public Box {
 public:
  Viewport() = default;
  ~Viewport() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Viewport);

 public:
  static inline auto New() -> Viewport* {
    return new Viewport();
  }
};
}  // namespace kura::elem

#endif  // KURA_ELEMENT_H
