#include "frontend/syntax/ast.h"

namespace kura::syntax {
auto FindPathAt(const Node* root, const uint32_t offset) -> std::vector<const Node*> {
  std::vector<const Node*> path{};
  if (root == nullptr || !root->span.Touches(offset))
    return path;
  const Node* current = root;
  while (current != nullptr) {
    path.push_back(current);
    // Prefer a child that strictly contains the offset, so that at a boundary
    // `a|b` the cursor belongs to the node it is about to type into.
    const Node* next = nullptr;
    const Node* touching = nullptr;
    current->ForEachChild([&](const Node* child) {
      if (child->span.Contains(offset))
        next = child;
      else if (touching == nullptr && child->span.Touches(offset))
        touching = child;
    });
    current = next != nullptr ? next : touching;
  }
  return path;
}

auto FindNodeAt(const Node* root, const uint32_t offset) -> const Node* {
  const auto path = FindPathAt(root, offset);
  return path.empty() ? nullptr : path.back();
}
}  // namespace kura::syntax
