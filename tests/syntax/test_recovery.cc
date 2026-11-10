#include <gtest/gtest.h>

#include "frontend/syntax/ast_printer.h"
#include "test_util.h"

namespace kura::syntax::test {
namespace {
auto Codes(const SyntaxTree& tree) -> std::vector<uint64_t> {
  std::vector<uint64_t> codes{};
  for (const auto& d : tree.GetDiagnostics())
    codes.push_back(d.code);
  return codes;
}

auto Lines(const SyntaxTree& tree) -> std::vector<uint32_t> {
  std::vector<uint32_t> lines{};
  for (const auto& d : tree.GetDiagnostics())
    lines.push_back(tree.GetSource().GetPosition(d.span.begin).line + 1);
  return lines;
}

auto FindFn(const SyntaxTree& tree, const std::string& name) -> const FnDecl* {
  for (const auto* item : tree.GetRoot()->items) {
    if (const auto* fn = item->As<FnDecl>(); fn != nullptr && fn->name.text == name)
      return fn;
  }
  return nullptr;
}
}  // namespace

using C = uint64_t;

// One check per diagnostic code: the right code, on the right text, and only one.
struct CodeCase {
  const char* source;
  DiagnosticCode code;
  const char* quoted;  // what the primary span should cover
};

class DiagnosticCodeTest : public testing::TestWithParam<CodeCase> {};

TEST_P(DiagnosticCodeTest, ReportsTheRightCodeOnTheRightText) {
  const auto& param = GetParam();
  const auto tree = Tree(param.source);
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U) << param.source << "\n"
                                               << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto& d = tree->GetDiagnostics().at(0);
  Render(std::cout, tree->GetSource(), d);
  EXPECT_EQ(d.code, param.code);
  EXPECT_EQ(std::string(tree->GetText(d.span)), param.quoted);
}

INSTANTIATE_TEST_SUITE_P(
    All, DiagnosticCodeTest,
    testing::Values(CodeCase{"let a = $", DiagnosticCode::kUnexpectedCharacter, "$"},
                    CodeCase{"let a = \"x\n", DiagnosticCode::kUnterminatedString, "\""},
                    CodeCase{"let a = \"\\q\"", DiagnosticCode::kInvalidEscape, "\\q"},
                    CodeCase{"extern fn `x(): Int", DiagnosticCode::kUnterminatedRawIdent, "`"},
                    CodeCase{"@ let a = 1", DiagnosticCode::kBadDirective, "@"},
                    CodeCase{"let a = b -c", DiagnosticCode::kBinaryMinusSpacing, "-"},
                    CodeCase{"let a = if x { 1 }", DiagnosticCode::kIfWithoutElse, "}"},
                    CodeCase{"fn f: Int => 1", DiagnosticCode::kFnWithoutParens, ":"},
                    CodeCase{"extern fn f(x: Int)", DiagnosticCode::kExternWithoutReturnType, ")"},
                    CodeCase{"let fn = 1", DiagnosticCode::kKeywordAsName, "fn"},
                    CodeCase{"let _ = 1", DiagnosticCode::kWildcardAsName, "_"},
                    CodeCase{"1 + 2", DiagnosticCode::kTopLevelExpression, "1 + 2"},
                    CodeCase{"let x = {}\nx.y = 1", DiagnosticCode::kAssignToProperty, "x.y ="},
                    CodeCase{"let x = { a: 1, 2 }", DiagnosticCode::kBadRecordEntry, "2"},
                    CodeCase{"let x = a |> f", DiagnosticCode::kPipeTargetNotCall, "f"},
                    CodeCase{"let x = for _, n in xs n", DiagnosticCode::kForWithoutArrow, "n"},
                    CodeCase{"use math", DiagnosticCode::kUseWithoutHeader, "math"},
                    CodeCase{"=> 1", DiagnosticCode::kExpectedItem, "=>"},
                    CodeCase{"let x = (1", DiagnosticCode::kMissingClosingDelimiter, "1"},
                    CodeCase{"let x = else", DiagnosticCode::kDuplicateElse, "else"},
                    CodeCase{"let x = match y { }", DiagnosticCode::kEmptyMatch, "match"},
                    CodeCase{"let x = \"{}\"", DiagnosticCode::kBadInterpolation, "{}"},
                    CodeCase{"let x = _", DiagnosticCode::kPlaceholderOutsideCall, "_"},
                    CodeCase{"let x = 1 |> f(_) <| 2", DiagnosticCode::kMixedPipes, "<|"},
                    CodeCase{"fn f() => #b(\"a\", x: 1)", DiagnosticCode::kPositionalNotLast, "x: 1"},
                    CodeCase{"let x = { p | }", DiagnosticCode::kBadRecordUpdate, "{ p | }"},
                    CodeCase{"let x = when y { 1 } else { 2 }", DiagnosticCode::kWhenWithElse, "else"},
                    CodeCase{"let x = for a, b, c in xs => 1", DiagnosticCode::kBadForBinding, ","},
                    CodeCase{"static-assert(true, 3)", DiagnosticCode::kBadStaticAssert, "3"}));

TEST(RecoveryTest, ABrokenDeclarationDoesNotHideTheNextOne) {
  const auto tree = Tree("let a =\nfn good() => 1\ntype T = A\n");
  EXPECT_FALSE(tree->IsValid());
  EXPECT_NE(FindFn(*tree, "good"), nullptr);
  ASSERT_EQ(tree->GetRoot()->items.size(), 3U);
  EXPECT_TRUE(tree->GetRoot()->items[2]->Is<TypeDecl>());
}

TEST(RecoveryTest, IndependentErrorsAreAllReported) {
  const auto tree = Tree(
      "let a =\n"              // 1: missing value
      "fn good() => 1\n"       // 2
      "let b = )\n"            // 3: stray closer
      "let c = if x { 1 }\n"   // 4: missing else
      "fn also-good() => 2\n"  // 5
      "type = A\n"             // 6: no type name
      "fn last() => 3\n");     // 7
  EXPECT_EQ(Lines(*tree), (std::vector<uint32_t>{1, 3, 4, 6}))
      << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  EXPECT_NE(FindFn(*tree, "good"), nullptr);
  EXPECT_NE(FindFn(*tree, "also-good"), nullptr);
  EXPECT_NE(FindFn(*tree, "last"), nullptr);
}

TEST(RecoveryTest, AnUnclosedParenDoesNotSwallowTheFollowingDeclarations) {
  const auto tree = Tree("fn f() => foo(1, 2\n\nfn g() => 1\n\ntype T = A\n");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  const auto& d = tree->GetDiagnostics().at(0);
  EXPECT_EQ(d.code, DiagnosticCode::kMissingClosingDelimiter);
  ASSERT_EQ(d.labels.size(), 1U);
  EXPECT_EQ(std::string(tree->GetText(d.labels[0].span)), "(");
  EXPECT_NE(FindFn(*tree, "g"), nullptr);
  EXPECT_TRUE(tree->GetRoot()->items.back()->Is<TypeDecl>());
}

TEST(RecoveryTest, AnUnclosedBraceDoesNotSwallowTheFollowingDeclarations) {
  const auto tree = Tree("fn f() => {\n  let a = 1\n\nfn g() => 2\nlet z = 3\n");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  EXPECT_EQ(tree->GetDiagnostics().at(0).code, DiagnosticCode::kMissingClosingDelimiter);
  EXPECT_NE(FindFn(*tree, "g"), nullptr);
  EXPECT_EQ(tree->GetRoot()->items.size(), 3U);
}

TEST(RecoveryTest, ABrokenElementKeepsItsGoodSiblings) {
  const auto tree = Tree("fn view() => #col {\n  #a\n  1 + 2\n  #b\n}\nfn after() => 1");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  const auto* col = FindFn(*tree, "view")->body->As<UiElement>();
  ASSERT_NE(col, nullptr);
  size_t elements = 0;
  for (const auto* child : col->children)
    elements += child->Is<UiElement>();
  EXPECT_EQ(elements, 2U);
  EXPECT_NE(FindFn(*tree, "after"), nullptr);
}

TEST(RecoveryTest, ABrokenArgumentKeepsItsGoodSiblings) {
  const auto tree = Tree("let x = f(1, , 3)");
  EXPECT_EQ(tree->GetDiagnostics().size(), 1U);
  const auto* call = tree->GetRoot()->items[0]->As<Binding>()->value->As<Call>();
  ASSERT_NE(call, nullptr);
  EXPECT_EQ(call->args.size(), 3U);
  EXPECT_EQ(Sexp(call->args[0]), "1");
  EXPECT_EQ(Sexp(call->args[2]), "3");
}

TEST(RecoveryTest, AMissingEqualsStillParsesTheValue) {
  const auto tree = Tree("let a 1 + 2\nlet b = 3");
  EXPECT_EQ(Codes(*tree), std::vector<uint64_t>{DiagnosticCode::kUnexpectedToken});
  EXPECT_EQ(tree->GetRoot()->items.size(), 2U);
}

// One mistake should be one diagnostic.
TEST(RecoveryTest, NoCascadesForTheCommonMistakes) {
  for (const auto* source : {
           "let a--b = 10",
           "use math",
           "extern sqrt(x: Float): Float",
           "let x = { 10, }",
           "let x = { get-data(), }",
           "fn f(a: Int b: Int) => a",
           "type T = A |",
           "let x = a |> ",
           "let x = (1 + ",
           "let x = [1, 2",
           "fn f() =>",
       }) {
    const auto tree = Tree(source);
    EXPECT_EQ(tree->GetDiagnostics().size(), 1U) << source << "\n"
                                                 << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  }
}

TEST(RecoveryTest, ErrorNodesAlwaysComeWithADiagnostic) {
  const auto tree = Tree("let x = \nlet y = + 1");
  std::vector<const Node*> nodes{};
  CollectNodes(tree->GetRoot(), nodes);
  size_t errors = 0;
  for (const auto* node : nodes)
    errors += node->Is<Error>();
  EXPECT_GT(errors, 0U);
  EXPECT_TRUE(tree->GetDiagnostics().HasErrors());
}

TEST(RecoveryTest, StrayClosersAreReportedAndSkipped) {
  const auto tree = Tree("} fn f() => 1 )");
  EXPECT_EQ(tree->GetDiagnostics().size(), 2U);
  EXPECT_NE(FindFn(*tree, "f"), nullptr);
}

TEST(RecoveryTest, TheBinaryMinusMistakeStillBuildsASubtraction) {
  const auto tree = Tree("let a = b -c");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  EXPECT_EQ(Sexp(tree->GetRoot()->items[0]->As<Binding>()->value), "(- b c)");
}

TEST(RecoveryTest, AnUnaryMinusOnTheNextLineIsNotMistakenForSubtraction) {
  // a statement that starts with `-` after a newline is a negation, as before
  const auto tree = Tree("fn f() => { let a = 1\n-a }");
  EXPECT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
}

// ---- rendering --------------------------------------------------------------

TEST(DiagnosticRenderTest, MatchesTheDocumentedShape) {
  const auto tree = Tree("fn get-pos: Position => { x: 1, }\n");
  const auto text = RenderToString(tree->GetSource(), tree->GetDiagnostics());
  EXPECT_EQ(text,
            "ERROR[K0009]: a function needs a parameter list, found `:`\n"
            " --> test.kui:1:11\n"
            "  |\n"
            "1 | fn get-pos: Position => { x: 1, }\n"
            "  | -- in this function\n"
            "  |           ^ expected `(` here\n"
            "  = help: functions always have parentheses, even with no parameters: `fn get-pos() => ...`\n"
            "\n");
}

TEST(DiagnosticRenderTest, TabsAndWideSpansLineUp) {
  const auto tree = Tree("let\tx = 1 +\n");
  const auto text = RenderToString(tree->GetSource(), tree->GetDiagnostics());
  // the tab is drawn as spaces, and the caret sits under the same place
  EXPECT_NE(text.find("1 | let x = 1 +"), std::string::npos) << text;
}

TEST(DiagnosticRenderTest, CodesHaveSummaries) {
  EXPECT_EQ(ToString(DiagnosticCode::kUnexpectedToken), "K0001");
  EXPECT_EQ(ToString(DiagnosticCode::kTooMuchNesting), "K0023");
  EXPECT_FALSE(GetSummary(DiagnosticCode::kIfWithoutElse).empty());
}
}  // namespace kura::syntax::test
