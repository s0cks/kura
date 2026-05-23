#ifndef KURA_DOM_H
#define KURA_DOM_H

#include <memory>
#include <string>
#include <vector>
#include <yoga/YGNode.h>
#include <yoga/Yoga.h>

#include "common.h"

namespace kura::dom {
#define FOR_EACH_BLOCK_NODE(V) \
  V(Block)                     \
  V(Button)                    \
  V(List)                      \
  V(Text)                      \
  V(Line)

#define FOR_EACH_INLINE_BLOCK_NODE(V) \
  V(InlineBlock)                      \
  V(Label)

#define FOR_EACH_DOM_NODE(V) \
  FOR_EACH_BLOCK_NODE(V)     \
  FOR_EACH_INLINE_BLOCK_NODE(V)

struct Node;
// clang-format off
#define DECLARE_DOM_TYPE(Name) \
  struct Name##Node;
FOR_EACH_DOM_NODE(DECLARE_DOM_TYPE)
#undef DECLARE_DOM_TYPE

enum NodeType {
  kUnknownNode = 0,
#define DEFINE_NODE_TYPE(Name) \
  k##Name##Node,
  FOR_EACH_DOM_NODE(DEFINE_NODE_TYPE)
#undef DEFINE_NODE_TYPE
};
// clang-format on
using NodePtr = std::shared_ptr<Node>;
using NodeList = std::vector<NodePtr>;

class NodeVisitor {
 public:
  NodeVisitor() = default;
  virtual ~NodeVisitor() = default;

#define DEFINE_VISIT_NODE(Name) virtual auto Visit##Name##Node(Name##Node* value) -> bool = 0;
  FOR_EACH_DOM_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
};

struct Node {
  NodeType type;
  YGNodeRef node;

  Node(const NodeType t) :
    type(t),
    node(YGNodeNew()) {}
  Node(const Node& rhs) = default;
  Node(Node&& rhs) = default;
  ~Node() {
    YGNodeFree(node);
  }

  auto operator=(const Node& rhs) -> Node& = default;
  auto operator=(Node&& rhs) -> Node& = default;

#define DEFINE_TYPE_CHECK(Name)                          \
  constexpr inline auto Is##Name##Node() const -> bool { \
    return type == k##Name##Node;                        \
  }
  FOR_EACH_DOM_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  constexpr inline auto IsBlock() const -> bool {
    switch (type) {
#define DEFINE_CHECK(Name) \
  case k##Name##Node:      \
    return true;
      FOR_EACH_BLOCK_NODE(DEFINE_CHECK)
#undef DEFINE_CHECK
      default:
        return false;
    }
  }

  constexpr inline auto IsInlineBlock() const -> bool {
    switch (type) {
#define DEFINE_CHECK(Name) \
  case k##Name##Node:      \
    return true;
      FOR_EACH_INLINE_BLOCK_NODE(DEFINE_CHECK)
#undef DEFINE_CHECK
      default:
        return false;
    }
  }
};

struct BlockNode : Node {
  BlockNode() :
    Node(kBlockNode) {}
  ~BlockNode() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(BlockNode);
};

static inline auto NewBlockNode() -> NodePtr {
  return std::make_shared<BlockNode>();
}

struct InlineBlockNode : Node {
  InlineBlockNode() :
    Node(kInlineBlockNode) {}
  ~InlineBlockNode() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(InlineBlockNode);
};

static inline auto NewInlineBlockPtr() -> NodePtr {
  return std::make_shared<InlineBlockNode>();
}

struct TextNode : Node {
  std::string text{};

  TextNode(const std::string value) :
    Node(kTextNode),
    text(value) {}
  ~TextNode() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(TextNode);
};

static inline auto NewTextNode(const std::string value) -> NodePtr {
  return std::make_shared<TextNode>(std::move(value));
}

struct ListNode : Node {
  NodeList children{};

  ListNode() :
    Node(kListNode) {}
  ListNode(const NodeList& values) :
    Node(kListNode),
    children(values) {}
  ~ListNode() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(ListNode);
};

static inline auto NewListNode() -> NodePtr {
  return std::make_shared<ListNode>();
}

static inline auto NewListNode(const NodeList& values) -> NodePtr {
  return std::make_shared<ListNode>(values);
}

struct LineNode : Node {
  LineNode() :
    Node(kLineNode) {}
  ~LineNode() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(LineNode);
};

static inline auto NewLineNode() -> NodePtr {
  return std::make_shared<LineNode>();
}
}  // namespace kura::dom

#endif  // KURA_DOM_H
