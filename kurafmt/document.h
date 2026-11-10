#ifndef KURA_DOCUMENT_H
#define KURA_DOCUMENT_H

#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "config.h"
#include "frontend/syntax/ast.h"

namespace kura::fmt {
struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Nil {};

struct Text {
  std::string text;
};

struct Line {
  bool soft = true;
  bool hard = false;
};

struct BreakIf {
  NodePtr when_break;
  NodePtr when_flat;
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
  std::variant<Nil, Text, Line, BreakIf, Indent, Concat, Group> value;
};

static inline auto NewEmpty() -> NodePtr {
  return std::make_shared<Node>(Nil{});
}

static inline auto NewText(const std::string_view text) -> NodePtr {
  return std::make_shared<Node>(Text{std::string(text)});
}

static inline auto NewLine(const bool soft = true, const bool hard = false) -> NodePtr {
  return std::make_shared<Node>(Line{soft, hard});
}

static inline auto NewSoftLine() -> NodePtr {
  return NewLine(false);
}

static inline auto NewHardLine() -> NodePtr {
  return NewLine(true, true);
}

static inline auto NewBreakIf(NodePtr broken, NodePtr flat) -> NodePtr {
  return std::make_shared<Node>(BreakIf{broken, flat});
}

static inline auto NewIndent(const int amt, NodePtr value) -> NodePtr {
  if (amt == 0)
    return value;

  return std::make_shared<Node>(Indent{amt, value});
}

static inline auto NewConcat(const std::vector<NodePtr> values) -> NodePtr {
  return std::make_shared<Node>(Concat{values});
}

static inline auto NewGroup(NodePtr node) -> NodePtr {
  return std::make_shared<Node>(Group{node});
}

static inline auto Punctuate(NodePtr separator, const std::vector<NodePtr>& elements) -> NodePtr {
  std::vector<NodePtr> nodes;
  for (size_t i = 0; i < elements.size(); ++i) {
    nodes.push_back(elements[i]);
    if (i + 1 < elements.size()) {
      nodes.push_back(separator);
    }
  }

  return NewConcat(nodes);
}

static inline auto Enclose(std::string_view open, std::string_view close, NodePtr body, int indent_amt = 2,
                           bool inner_space = true, bool newline_before = false, bool newline_after = false)
    -> NodePtr {
  std::vector<NodePtr> parts{};
  parts.push_back(NewText(open));
  parts.push_back(NewIndent(indent_amt, inner_space ? NewConcat({NewLine(newline_before), body}) : body));
  if (inner_space)
    parts.push_back(NewLine(newline_after));
  parts.push_back(NewText(close));
  return NewGroup(NewConcat(parts));
}

static inline auto Quote(NodePtr body, const bool single = false) -> NodePtr {
  const auto quote = single ? NewText("'") : NewText("\"");
  return NewConcat({
      quote,
      body,
      quote,
  });
}

static inline auto DoubleQuote(NodePtr rhs) -> NodePtr {
  return Quote(rhs, false);
}

static inline auto SingleQuote(NodePtr rhs) -> NodePtr {
  return Quote(rhs, true);
}

auto Lower(const Config& config, const syntax::Node& node) -> NodePtr;
auto Render(NodePtr root, const int max_width) -> std::string;
}  // namespace kura::fmt

#endif  // KURA_DOCUMENT_H
