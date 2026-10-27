#include "dom/node.h"

#include <algorithm>

namespace kura::dom {
auto Visitor::VisitNode(Node* node) -> bool {
  return VisitChildren(node);
}

auto Visitor::VisitChildren(Node* node) -> bool {
  for (const auto child : node->GetChildren()) {
    if (!child->Accept(this))
      return false;
  }

  return true;
}

void Node::Append(Node* child) {
  if (!child || child == this)
    return;

  if (const auto old = child->parent_) {
    auto& siblings = old->children_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
  }

  child->parent_ = this;
  children_.push_back(child);
}

auto Node::FindProperty(const PropertyDef* def) const -> const PropertyValue* {
  for (const auto& property : properties_) {
    if (property.def == def)
      return &property;
  }

  return nullptr;
}

auto Node::FindProperty(const std::string_view name) const -> const PropertyValue* {
  for (const auto& property : properties_) {
    if (property.def->name == name)
      return &property;
  }

  return nullptr;
}

void Node::SetProperty(const PropertyValue& value) {
  for (auto& property : properties_) {
    if (property.def == value.def) {
      property = value;
      return;
    }
  }

  properties_.push_back(value);
}

auto Node::Accept(Visitor* vis) -> bool {
  switch (kind_) {
#define DEFINE_CASE(Name, Tag) \
  case Kind::k##Name:          \
    return vis->Visit##Name(this);

    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
    case Kind::kDynamic:
      return vis->VisitDynamic(this);
    case Kind::kBind:
      return vis->VisitBind(this);
    default:
      return false;
  }
}

Tree::Tree() :
  root_(nullptr) {
  root_ = New(Kind::kFragment);
}

auto Tree::New(const Kind kind) -> Node* {
  nodes_.push_back(std::unique_ptr<Node>(new Node(kind)));
  return nodes_.back().get();
}
}  // namespace kura::dom
