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
#include "element_property.h"

namespace kura::elem {
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

#define DECLARE_ELEMENT_NODE_TYPE(Name)               \
  DEFINE_NON_COPYABLE_TYPE(Name);                     \
                                                      \
 public:                                              \
  auto Accept(NodeVisitor* vis) -> bool override;     \
  auto GetName() const -> std::string_view override { \
    return #Name;                                     \
  }                                                   \
  auto As##Name() -> Name* override {                 \
    return this;                                      \
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

  DECLARE_ELEMENT_NODE_TYPE(Box);

 public:
  static inline auto New() -> Box* {
    return new Box();
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
