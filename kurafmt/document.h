#ifndef KURA_DOCUMENT_H
#define KURA_DOCUMENT_H

#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "frontend/syntax/ast.h"

namespace kura::fmt {
struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Nil {};

struct Text {
  std::string_view text;
};

struct Line {
  bool hard = false;
};

struct Indent {
  int amount;
  NodePtr node;
};

struct Concat {
  std::vector<NodePtr> nodes;
};

struct Group {
  NodePtr node;
};

struct Node {
  std::variant<Nil, Text, Line, Indent, Concat, Group> value;
};

static inline auto NewText(const std::string_view text) -> NodePtr {
  return std::make_shared<Node>(Text{text});
}

static inline auto NewLine(const bool hard = false) -> NodePtr {
  return std::make_shared<Node>(Line{hard});
}

static inline auto NewIndent(const int amt, NodePtr value) -> NodePtr {
  return std::make_shared<Node>(Indent{amt, value});
}

static inline auto NewConcat(const std::vector<NodePtr> values) -> NodePtr {
  return std::make_shared<Node>(Concat{values});
}

static inline auto NewGroup(NodePtr node) -> NodePtr {
  return std::make_shared<Node>(Group{node});
}

auto Build(syntax::Node* node) -> NodePtr;
auto Render(NodePtr root, const int max_width) -> std::string;
}  // namespace kura::fmt

#endif  // KURA_DOCUMENT_H
