#include <gtest/gtest.h>

#include "lsp/code_action.h"
#include "lsp/keyword_completion.h"

namespace kura::lsp {
namespace {
// `|` marks the cursor.
auto Complete(std::string text) -> std::vector<CompletionItem> {
  const auto offset = text.find('|');
  text.erase(offset, 1);
  Document doc("file:///test.kui", text);
  return GetDeclarationCompletions(doc, static_cast<uint32_t>(offset));
}

// Applies every edit of `action` to `text`, last edit first.
auto Apply(const Document& doc, const CodeAction& action) -> std::string {
  auto text = doc.GetContents();
  for (auto it = action.edits.rbegin(); it != action.edits.rend(); ++it) {
    const auto begin = doc.ToOffset(it->range.start);
    const auto end = doc.ToOffset(it->range.end);
    text.replace(begin, end - begin, it->newText);
  }
  return text;
}

auto Fix(const std::string& text, const Range& range, const bool index = true) -> std::vector<std::string> {
  Document doc("file:///test.kui", text);
  if (index)
    doc.SetSyntaxTree(syntax::ParseText("test.kui", text), doc.GetVersion());

  std::vector<std::string> results{};
  for (const auto& action : GetCodeActions(doc, range))
    results.push_back(Apply(doc, action));
  return results;
}

const Range kWholeFile{{0, 0}, {100, 0}};
}  // namespace

TEST(KeywordCompletionTest, FnAtTopLevel) {
  const auto items = Complete("fn|");
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].label, "fn");
  EXPECT_TRUE(items[0].preselect);
  EXPECT_EQ(items[0].insertTextFormat, CompletionItem::kSnippetFormat);
  ASSERT_TRUE(items[0].textEdit);
  EXPECT_EQ(items[0].textEdit->range.start.character, 0);
  EXPECT_EQ(items[0].textEdit->range.end.character, 2);
}

TEST(KeywordCompletionTest, TypeAtTopLevel) {
  const auto items = Complete("type|");
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].label, "type");
  EXPECT_NE(items[0].insertText.find("type ${1:Name} = {"), std::string::npos);
}

TEST(KeywordCompletionTest, PrefixesMatchKeywords) {
  EXPECT_EQ(Complete("f|").at(0).label, "fn");
  EXPECT_EQ(Complete("ty|").at(0).label, "type");
  EXPECT_FALSE(Complete("ty|").at(0).preselect);
  EXPECT_EQ(Complete("|").size(), 2u);
}

TEST(KeywordCompletionTest, IndentedAfterOtherDeclarations) {
  EXPECT_EQ(Complete("type A = Int\n\n  fn|").size(), 1u);
}

TEST(KeywordCompletionTest, NotInsideBodies) {
  EXPECT_TRUE(Complete("fn view(): Content => {\n  fn|\n}").empty());
  EXPECT_TRUE(Complete("fn view(x: Int): Int => (\n  type|\n)").empty());
}

TEST(KeywordCompletionTest, NotMidLine) {
  EXPECT_TRUE(Complete("let x = 1 fn|").empty());
}

TEST(KeywordCompletionTest, NotForOtherWords) {
  EXPECT_TRUE(Complete("my-fn|").empty());
  EXPECT_TRUE(Complete("fnord|").empty());
}

TEST(KeywordCompletionTest, BracesInStringsAndCommentsDoNotCount) {
  EXPECT_EQ(Complete("// {\nfn|").size(), 1u);
  EXPECT_EQ(Complete("let s = \"{\"\nfn|").size(), 1u);
}

TEST(KeywordCompletionTest, RangeIsInUtf16Columns) {
  const auto items = Complete("let s = \"😀\"\nfn|");
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].textEdit->range.start.line, 1);
  EXPECT_EQ(items[0].textEdit->range.start.character, 0);
}

TEST(CodeActionTest, AddsParensToFnWithoutParameterList) {
  const auto fixed = Fix("fn main => 1\n", kWholeFile);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0], "fn main() => 1\n");
}

TEST(CodeActionTest, KeepsTheResultType) {
  const auto fixed = Fix("fn main: Int => 1\n", kWholeFile);
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0], "fn main(): Int => 1\n");
}

TEST(CodeActionTest, NothingForValidFn) {
  EXPECT_TRUE(Fix("fn main() => 1\n", kWholeFile).empty());
}

TEST(CodeActionTest, OnlyWhereTheCursorIs) {
  const std::string text = "type A = Int\nfn go => 1\n\nfn ok() => 2\n";
  EXPECT_TRUE(Fix(text, Range{{0, 0}, {0, 3}}).empty());
  EXPECT_TRUE(Fix(text, Range{{3, 0}, {3, 3}}).empty());

  const auto fixed = Fix(text, Range{{1, 4}, {1, 4}});
  ASSERT_EQ(fixed.size(), 1u);
  EXPECT_EQ(fixed[0], "type A = Int\nfn go() => 1\n\nfn ok() => 2\n");
}

TEST(CodeActionTest, NothingWithoutATree) {
  EXPECT_TRUE(Fix("fn main => 1\n", kWholeFile, false).empty());
}

TEST(CodeActionTest, NothingWhenTheTreeIsOlderThanTheText) {
  DocumentStore store;
  store.Update("u", "fn a => 1");
  auto* doc = store.Get("u");
  doc->SetSyntaxTree(syntax::ParseText("u", doc->GetContents()), doc->GetVersion());
  EXPECT_EQ(GetCodeActions(*doc, kWholeFile).size(), 1u);

  store.Update("u", "fn a => 12");
  EXPECT_TRUE(GetCodeActions(*doc, kWholeFile).empty());
}

TEST(DocumentTest, ConvertsBetweenUtf16PositionsAndByteOffsets) {
  // a = 1 unit, é = 1, 😀 = 2 (surrogate pair), b = 1
  Document doc("file:///test.kui", "aé😀b\nxy");
  EXPECT_EQ(doc.ToOffset({0, 4}), 1u + 2u + 4u);
  EXPECT_EQ(doc.ToPosition(7).character, 4);
  EXPECT_EQ(doc.ToOffset({1, 1}), doc.GetContents().find("xy") + 1);
  EXPECT_EQ(doc.ToOffset({9, 0}), doc.GetContents().size());
}
}  // namespace kura::lsp
