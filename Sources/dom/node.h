#ifndef KURA_DOM_NODE_H
#define KURA_DOM_NODE_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "common.h"
#include "dom/kind.h"
#include "dom/property.h"

namespace kura {
class Object;
namespace expr {
class Expr;
}  // namespace expr

namespace dom {
class Node;
class Tree;

class Visitor {
 protected:
  Visitor() = default;

  auto VisitChildren(Node* node) -> bool;

 public:
  virtual ~Visitor() = default;

  virtual auto VisitNode(Node* node) -> bool;

#define DEFINE_VISIT(Name, Tag)                  \
  virtual auto Visit##Name(Node* node) -> bool { \
    return VisitNode(node);                      \
  }

  FOR_EACH_ELEMENT_NODE(DEFINE_VISIT)
#undef DEFINE_VISIT
  virtual auto VisitDynamic(Node* node) -> bool {
    return VisitNode(node);
  }
  virtual auto VisitBind(Node* node) -> bool {
    return VisitNode(node);
  }
};

struct PropertyValue {
  const PropertyDef* def = nullptr;
  Object* constant = nullptr;
  expr::Expr* expr = nullptr;

  inline auto IsConstant() const -> bool {
    return constant != nullptr;
  }
};

using NodeList = std::vector<Node*>;
using PropertyValueList = std::vector<PropertyValue>;
using TagList = std::vector<std::string>;

class Node {
  DEFINE_NON_COPYABLE_TYPE(Node);
  friend class Tree;

 private:
  Kind kind_;
  Node* parent_ = nullptr;
  NodeList children_{};
  PropertyValueList properties_{};
  expr::Expr* expr_ = nullptr;
  TagList tags_{};

  explicit Node(const Kind kind) :
    kind_(kind) {}

 public:
  ~Node() = default;

  inline auto GetKind() const -> Kind {
    return kind_;
  }

  inline auto GetName() const -> std::string_view {
    return KindName(kind_);
  }

  inline auto GetTag() const -> std::string_view {
    return KindTag(kind_);
  }

#define DEFINE_KIND_CHECK(Name, Tag)     \
  inline auto Is##Name() const -> bool { \
    return kind_ == Kind::k##Name;       \
  }

  FOR_EACH_ELEMENT_NODE(DEFINE_KIND_CHECK)
#undef DEFINE_KIND_CHECK

  inline auto IsDynamic() const -> bool {
    return kind_ == Kind::kDynamic;
  }

  inline auto IsBind() const -> bool {
    return kind_ == Kind::kBind;
  }

  inline auto IsElement() const -> bool {
    return dom::IsElement(kind_);
  }

  inline auto ReservesSpace() const -> bool {
    return dom::ReservesSpace(kind_);
  }

  inline auto GetParent() const -> Node* {
    return parent_;
  }

  inline auto GetChildren() const -> const NodeList& {
    return children_;
  }

  inline auto GetNumberOfChildren() const -> size_t {
    return children_.size();
  }

  inline auto HasChildren() const -> bool {
    return !children_.empty();
  }

  inline auto GetChildAt(const size_t idx) const -> Node* {
    return idx < children_.size() ? children_[idx] : nullptr;
  }

  void Append(Node* child);

  inline auto GetProperties() const -> const PropertyValueList& {
    return properties_;
  }

  inline auto HasProperties() const -> bool {
    return !properties_.empty();
  }

  auto FindProperty(const PropertyDef* def) const -> const PropertyValue*;
  auto FindProperty(std::string_view name) const -> const PropertyValue*;

  inline auto HasProperty(const std::string_view name) const -> bool {
    return FindProperty(name) != nullptr;
  }

  void SetProperty(const PropertyValue& value);

  inline auto GetExpr() const -> expr::Expr* {
    return expr_;
  }

  inline void SetExpr(expr::Expr* rhs) {
    expr_ = rhs;
  }

  inline auto GetTags() const -> const TagList& {
    return tags_;
  }

  inline void SetTags(TagList rhs) {
    tags_ = std::move(rhs);
  }

  auto Accept(Visitor* vis) -> bool;
};

class Tree {
  DEFINE_NON_COPYABLE_TYPE(Tree);

 private:
  std::vector<std::unique_ptr<Node>> nodes_{};
  Node* root_;

 public:
  Tree();
  ~Tree() = default;

  inline auto GetRoot() const -> Node* {
    return root_;
  }

  inline auto GetNumberOfNodes() const -> size_t {
    return nodes_.size();
  }

  auto New(Kind kind) -> Node*;
};
}  // namespace dom
}  // namespace kura

#endif  // KURA_DOM_NODE_H
