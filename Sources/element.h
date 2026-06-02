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
  V(Block)                       \
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

  virtual void AddChild(Node* node) {
    // do nothing
  }

  virtual void SetChildAt(const size_t idx, Node* value) {
    // do nothing
  }

 public:
  virtual ~Node() = default;
  virtual auto Accept(NodeVisitor* vis) -> bool = 0;
  virtual auto GetName() const -> std::string_view = 0;

  virtual auto GetNumberOfChildren() const -> size_t {
    return 0;
  }

  virtual auto GetChildAt(const size_t idx) const -> Node* {
    return nullptr;
  }

  inline auto HasChildAt(const size_t idx) const -> bool {
    return GetChildAt(idx) != nullptr;
  }

  virtual auto HasChildren() const -> bool {
    return false;
  }

  virtual auto VisitChildren(NodeVisitor* vis) -> bool {
    return true;
  }

  virtual auto VisitChildren(std::function<bool(Node*)> vis) -> bool {
    return true;
  }

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

class StructuralNode : public Node {
 private:
  NodeList children_{};

 protected:
  explicit StructuralNode(const NodeList children = {}) :
    children_(children) {}

  void AddChild(Node* node) override {
    children_.push_back(node);
  }

  void SetChildAt(const size_t idx, Node* value) override {
    children_.at(idx) = value;
  }

 public:
  ~StructuralNode() = default;

  auto GetChildren() const -> const NodeList& {
    return children_;
  }

  auto GetNumberOfChildren() const -> size_t override {
    return children_.size();
  }

  auto HasChildren() const -> bool override {
    return !children_.empty();
  }

  auto GetChildAt(const size_t idx) const -> Node* override {
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

class Document : public StructuralNode {
 public:
  explicit Document(const NodeList children) :
    StructuralNode(std::move(children)) {}
  ~Document() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Document);

 public:
  static inline auto New(const NodeList children = {}) -> Document* {
    return new Document(children);
  }
};

class Fragment : public StructuralNode {
 public:
  explicit Fragment(const NodeList children) :
    StructuralNode(std::move(children)) {}
  ~Fragment() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Fragment);

 public:
  static inline auto New(const NodeList children = {}) -> Fragment* {
    return new Fragment(children);
  }
};

class LayoutNode : public Node {
 private:
  YGNodeRef node_ = nullptr;
  Property* properties_ = nullptr;

 public:
  LayoutNode() = default;
  ~LayoutNode() override = default;

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
};

template <const uint64_t NumberOfChildren>
class TemplateLayoutNode : public LayoutNode {
 private:
  Node* children_[NumberOfChildren];

 protected:
  TemplateLayoutNode() = default;

  void AddChild(Node* node) override {
    // do nothing
  }

 public:
  ~TemplateLayoutNode() override = default;

  auto GetNumberOfChildren() const -> size_t override {
    return NumberOfChildren;
  }

  auto HasChildren() const -> bool override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (children_[idx] != nullptr)
        return true;
    }
    return false;
  }

  auto GetChildAt(const size_t idx) const -> Node* override {
    return children_[idx];
  }

  auto VisitChildren(NodeVisitor* vis) -> bool override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (!children_[idx])
        continue;
      if (!children_[idx]->Accept(vis))
        return false;
    }
    return true;
  }

  auto VisitChildren(std::function<bool(Node*)> vis) -> bool override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (!children_[idx])
        continue;
      if (!vis(children_[idx]))
        return false;
    }
    return true;
  }
};

class DynamicLayoutNode : public LayoutNode {
 private:
  NodeList children_{};

 protected:
  explicit DynamicLayoutNode(const NodeList children) :
    LayoutNode(),
    children_(std::move(children)) {}

  void AddChild(Node* node) override {
    children_.push_back(node);
  }

 public:
  ~DynamicLayoutNode() override = default;

  auto GetChildren() const -> const NodeList& {
    return children_;
  }

  auto GetNumberOfChildren() const -> size_t override {
    return children_.size();
  }

  auto GetChildAt(const size_t idx) const -> Node* override {
    return children_.at(idx);
  }

  auto HasChildren() const -> bool override {
    return !children_.empty();
  }

  auto VisitChildren(NodeVisitor* vis) -> bool override;
  auto VisitChildren(std::function<bool(Node*)> vis) -> bool override;
};

class Block : public DynamicLayoutNode {
 public:
  explicit Block(const NodeList children) :
    DynamicLayoutNode(std::move(children)) {}
  ~Block() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Block);

 public:
  static inline auto New(const NodeList children = {}) -> Block* {
    return new Block(std::move(children));
  }
};

class Text : public LayoutNode {
 private:
  std::string value_;

 public:
  Text(const std::string value) :
    LayoutNode(),
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

class Button : public TemplateLayoutNode<1> {
  static constexpr const size_t kTextPos = 0;

 private:
  inline void SetText(Text* value) {
    SetChildAt(kTextPos, value);
  }

 public:
  explicit Button(Text* text) {
    SetText(text);
  }
  ~Button() override = default;

  auto GetText() const -> Text* {
    const auto text = GetChildAt(kTextPos);
    return text ? text->AsText() : nullptr;
  }

  inline auto HasText() const -> bool {
    return GetText() != nullptr;
  }

  DECLARE_ELEMENT_NODE_TYPE(Button);

 public:
  static inline auto New(Text* text = nullptr) -> Button* {
    return new Button(text);
  }
};

class Image : public LayoutNode {
 public:
  Image() = default;
  ~Image() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Image);

 public:
  static inline auto New() -> Image* {
    return new Image();
  }
};

class Canvas : public LayoutNode {
 public:
  Canvas() = default;
  ~Canvas() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Canvas);

 public:
  static inline auto New() -> Canvas* {
    return new Canvas();
  }
};

class Input : public LayoutNode {
 public:
  Input() = default;
  ~Input() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Input);

 public:
  static inline auto New() -> Input* {
    return new Input();
  }
};

class Scroll : public LayoutNode {
 public:
  Scroll() = default;
  ~Scroll() override = default;

  DECLARE_ELEMENT_NODE_TYPE(Scroll);

 public:
  static inline auto New() -> Scroll* {
    return new Scroll();
  }
};

class List : public DynamicLayoutNode {
 public:
  explicit List(const NodeList children) :
    DynamicLayoutNode(std::move(children)) {}
  ~List() override = default;

  DECLARE_ELEMENT_NODE_TYPE(List);

 public:
  static inline auto New(const NodeList children = {}) -> List* {
    return new List(std::move(children));
  }
};

class Viewport : public LayoutNode {
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
