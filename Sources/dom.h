#ifndef KURA_DOM_H
#define KURA_DOM_H

#include <memory>
#include <string>
#include <vector>
#include <yoga/YGNode.h>
#include <yoga/Yoga.h>

namespace kura::dom {
#define FOR_EACH_BLOCK_NODE(V)                                                 \
  V(Block)                                                                     \
  V(Button)                                                                    \
  V(List)                                                                      \
  V(Text)
#define FOR_EACH_INLINE_BLOCK_NODE(V)                                          \
  V(InlineBlock)                                                               \
  V(Label)
#define FOR_EACH_DOM_NODE(V)                                                   \
  FOR_EACH_BLOCK_NODE(V)                                                       \
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

#define DEFINE_VISIT_NODE(Name)                                                \
  virtual auto Visit##Name##Node(Name##Node *value) -> bool = 0;
  FOR_EACH_DOM_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
};

struct Node {
  NodeType type;
  YGNodeRef node;

  Node(const NodeType t) : type(t), node(YGNodeNew()) {}
  Node(const Node &rhs) = default;
  Node(Node &&rhs) = default;
  ~Node() { YGNodeFree(node); }

  auto operator=(const Node &rhs) -> Node & = default;
  auto operator=(Node &&rhs) -> Node & = default;

#define DEFINE_TYPE_CHECK(Name)                                                \
  constexpr inline auto Is##Name##Node() const -> bool {                       \
    return type == k##Name##Node;                                              \
  }
  FOR_EACH_DOM_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  constexpr inline auto IsBlock() const -> bool {
    switch (type) {
#define DEFINE_CHECK(Name)                                                     \
  case k##Name##Node:                                                          \
    return true;
      FOR_EACH_BLOCK_NODE(DEFINE_CHECK)
#undef DEFINE_CHECK
    default:
      return false;
    }
  }

  constexpr inline auto IsInlineBlock() const -> bool {
    switch (type) {
#define DEFINE_CHECK(Name)                                                     \
  case k##Name##Node:                                                          \
    return true;
      FOR_EACH_INLINE_BLOCK_NODE(DEFINE_CHECK)
#undef DEFINE_CHECK
    default:
      return false;
    }
  }
};

struct BlockNode : Node {
  BlockNode() : Node(kBlockNode) {}
  BlockNode(const BlockNode &rhs) = default;
  BlockNode(BlockNode &&rhs) = default;
  ~BlockNode() = default;

  auto operator=(const BlockNode &rhs) -> BlockNode & = default;
  auto operator=(BlockNode &&rhs) -> BlockNode & = default;
};

static inline auto NewBlockNode() -> NodePtr {
  return std::make_shared<BlockNode>();
}

struct InlineBlockNode : Node {
  InlineBlockNode() : Node(kInlineBlockNode) {}
  InlineBlockNode(const InlineBlockNode &rhs) = default;
  InlineBlockNode(InlineBlockNode &&rhs) = default;
  ~InlineBlockNode() = default;

  auto operator=(const InlineBlockNode &rhs) -> InlineBlockNode & = default;
  auto operator=(InlineBlockNode &&rhs) -> InlineBlockNode & = default;
};

static inline auto NewInlineBlockPtr() -> NodePtr {
  return std::make_shared<InlineBlockNode>();
}

struct TextNode : Node {
  std::string text{};

  TextNode(const std::string value) : Node(kTextNode), text(value) {}
  TextNode(TextNode &&rhs) = default;
  TextNode(const TextNode &rhs) = default;
  ~TextNode() = default;

  auto operator=(const TextNode &rhs) -> TextNode & = default;
  auto operator=(TextNode &&rhs) -> TextNode & = default;
};

static inline auto NewTextNode(const std::string value) -> NodePtr {
  return std::make_shared<TextNode>(std::move(value));
}

struct ListNode : Node {
  NodeList children{};

  ListNode() : Node(kListNode) {}
  ListNode(const NodeList &values) : Node(kListNode), children(values) {}
  ListNode(const ListNode &rhs) = default;
  ListNode(ListNode &&rhs) = default;
  ~ListNode() = default;

  auto operator=(const ListNode &rhs) -> ListNode & = default;
  auto operator=(ListNode &&rhs) -> ListNode & = default;
};

static inline auto NewListNode() -> NodePtr {
  return std::make_shared<ListNode>();
}

static inline auto NewListNode(const NodeList &values) -> NodePtr {
  return std::make_shared<ListNode>(values);
}
} // namespace kura::dom

#endif // KURA_DOM_H
