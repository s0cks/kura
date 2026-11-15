#include <gtest/gtest.h>
#include <string>

#include "bbcode.h"
#include "gtest/gtest.h"

class bbcode_parser_test : public ::testing::Test {
 public:
  bbcode_parser_test() = default;
  ~bbcode_parser_test() override = default;
};

static inline auto IsText(const bbcode_node* node) -> testing::AssertionResult {
  if (!node || node->kind != kTextNode)
    return ::testing::AssertionFailure() << "node is null or not a text node";
  return ::testing::AssertionSuccess();
}

static inline auto IsText(const bbcode_node* node, const std::string rhs) -> testing::AssertionResult {
  if (!node || node->kind != kTextNode)
    return ::testing::AssertionFailure() << "node is null or not a text node";
  const std::string node_text(node->text.start, node->text.length);
  if (node_text != rhs)
    return testing::AssertionFailure() << "not text `" << node_text << "` does not equal: " << rhs;
  return ::testing::AssertionSuccess();
}

static inline auto FirstChild(const bbcode_node_list& list) -> const bbcode_node* {
  return list.items && list.count > 0 ? &list.items[0] : (const bbcode_node*)nullptr;
}

template <typename T>
concept has_children = requires(T value) {
  { value.children };
};

template <typename T>
static inline auto FirstChild(const T& node)
  requires has_children<T>
{
  return FirstChild((const bbcode_node_list&)node.children);
}

TEST_F(bbcode_parser_test, TestParse_BoldNodeSuccessful) {
  std::string code = "[b]Hello World[/b]";

  static constexpr const auto kNumberOfNodesInArena = 1024;
  static constexpr const auto kTotalArenaSize = sizeof(bbcode_node) * kNumberOfNodesInArena;
  bbcode_node* arena = (bbcode_node*)malloc(kTotalArenaSize);
  memset(arena, 0, kTotalArenaSize);

  bbcode_parser parser{};
  bbcode_parser_init(&parser, code.c_str(), code.size(), arena, kTotalArenaSize);
  const auto result = bbcode_parse(&parser);
  ASSERT_EQ(result.count, 1);
  const auto* node = FirstChild(result);
  ASSERT_EQ(node->kind, kSpanNode);
  ASSERT_EQ(node->style.format, kBoldFormat);
  ASSERT_TRUE(IsText(FirstChild(node->style), "Hello World"));
}

TEST_F(bbcode_parser_test, TestParse_BoldNodeFails_Unclosed) {
  std::string code = "[b]Hello World";

  static constexpr const auto kNumberOfNodesInArena = 1024;
  static constexpr const auto kTotalArenaSize = sizeof(bbcode_node) * kNumberOfNodesInArena;
  bbcode_node* arena = (bbcode_node*)malloc(kTotalArenaSize);
  memset(arena, 0, kTotalArenaSize);

  bbcode_parser parser{};
  bbcode_parser_init(&parser, code.c_str(), code.size(), arena, kTotalArenaSize);
  const auto result = bbcode_parse(&parser);
  ASSERT_EQ(result.count, 1);
  const auto* node = &result.items[0];
  ASSERT_EQ(node->kind, kSpanNode);
  ASSERT_EQ(node->style.format, kBoldFormat);
  ASSERT_TRUE(IsText(FirstChild(node->style), "Hello World"));
}

TEST_F(bbcode_parser_test, TestParse_BoldNodeFails_MalformedCloseTag1) {
  std::string code = "[b]Hello World[/b";

  static constexpr const auto kNumberOfNodesInArena = 1024;
  static constexpr const auto kTotalArenaSize = sizeof(bbcode_node) * kNumberOfNodesInArena;
  bbcode_node* arena = (bbcode_node*)malloc(kTotalArenaSize);
  memset(arena, 0, kTotalArenaSize);

  bbcode_parser parser{};
  bbcode_parser_init(&parser, code.c_str(), code.size(), arena, kTotalArenaSize);
  const auto result = bbcode_parse(&parser);
  ASSERT_EQ(result.count, 1);
  const auto* node = &result.items[0];
  ASSERT_EQ(node->kind, kSpanNode);
  ASSERT_EQ(node->style.format, kBoldFormat);
  ASSERT_TRUE(IsText(FirstChild(node->style), "Hello World"));
}

TEST_F(bbcode_parser_test, TestParse_BoldNodeFails_MalformedCloseTag2) {
  std::string code = "[b]Hello World[";

  static constexpr const auto kNumberOfNodesInArena = 1024;
  static constexpr const auto kTotalArenaSize = sizeof(bbcode_node) * kNumberOfNodesInArena;
  bbcode_node* arena = (bbcode_node*)malloc(kTotalArenaSize);
  memset(arena, 0, kTotalArenaSize);

  bbcode_parser parser{};
  bbcode_parser_init(&parser, code.c_str(), code.size(), arena, kTotalArenaSize);
  const auto result = bbcode_parse(&parser);
  ASSERT_EQ(result.count, 1);
  const auto* node = &result.items[0];
  ASSERT_EQ(node->kind, kSpanNode);
  ASSERT_EQ(node->style.format, kBoldFormat);
  ASSERT_TRUE(IsText(FirstChild(node->style), "Hello World"));
}
