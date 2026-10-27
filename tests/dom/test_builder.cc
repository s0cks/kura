#include <gtest/gtest.h>
#include <sstream>

#include "dom/builder.h"
#include "dom/printer.h"
#include "frontend/expr.h"
#include "local_scope.h"
#include "object.h"
#include "property.h"
#include "type.h"

using namespace kura;
using namespace kura::expr;

namespace {
auto Prop(const std::string& name, Expr* value) -> StorePropertyExpr* {
  return StorePropertyExpr::New(Property::FindOrCreate(String::New(name), Type::NoneType()), value);
}

auto Lit(Object* value) -> Expr* {
  return LiteralExpr::New(value);
}

// A NodeExpr whose children a test can set: only the parser may append to one.
class TestNode : public NodeExpr {
 public:
  TestNode(const Kind kind, const PropertyList properties) :
    NodeExpr(kind, properties) {}

  using NodeExpr::Append;
};

auto Element(const NodeExpr::Kind kind, const NodeExpr::PropertyList properties = {}, const ExprList children = {})
    -> NodeExpr* {
  const auto node = new TestNode(kind, properties);
  for (const auto child : children)
    node->Append(child);
  return node;
}

struct Built {
  dom::Tree tree{};
  std::vector<std::string> errors{};
  dom::Node* root = nullptr;
};

auto Build(Expr* expr, Built* built) -> bool {
  dom::Builder builder(&built->tree);
  built->root = builder.Build(expr);
  built->errors = builder.GetErrors();
  return builder.Ok();
}

auto Dump(Built* built) -> std::string {
  std::stringstream ss{};
  dom::Printer::Print(ss, built->root);
  return ss.str();
}
}  // namespace

TEST(DomBuilder, ElementsAndChildren) {
  Built built{};
  const auto button =
      Element(NodeExpr::kButton, {}, {Element(NodeExpr::kText, {Prop("value", Lit(String::New("Go")))})});
  ASSERT_TRUE(Build(button, &built));

  ASSERT_EQ(built.root->GetNumberOfChildren(), 1);
  const auto node = built.root->GetChildAt(0);
  EXPECT_TRUE(node->IsButton());
  EXPECT_EQ(node->GetParent(), built.root);
  ASSERT_EQ(node->GetNumberOfChildren(), 1);
  const auto text = node->GetChildAt(0);
  EXPECT_TRUE(text->IsText());
  const auto value = text->FindProperty("value");
  ASSERT_NE(value, nullptr);
  ASSERT_TRUE(value->IsConstant());
  EXPECT_EQ(static_cast<String*>(value->constant)->GetValue(), "Go");
}

TEST(DomBuilder, SequenceRendersEveryNodeInOrder) {
  Built built{};
  ASSERT_TRUE(Build(
      SeqExpr::New(ExprList{Element(NodeExpr::kText), Element(NodeExpr::kBlock), Element(NodeExpr::kText)}), &built));
  ASSERT_EQ(built.root->GetNumberOfChildren(), 3);
  EXPECT_TRUE(built.root->GetChildAt(0)->IsText());
  EXPECT_TRUE(built.root->GetChildAt(1)->IsBlock());
  EXPECT_TRUE(built.root->GetChildAt(2)->IsText());
}

TEST(DomBuilder, NoneIsNoNode) {
  Built built{};
  ASSERT_TRUE(Build(SeqExpr::New(ExprList{Lit(None::Get()), Element(NodeExpr::kText)}), &built));
  EXPECT_EQ(built.root->GetNumberOfChildren(), 1);
}

TEST(DomBuilder, FoldsUnitArithmetic) {
  Built built{};
  const auto sum = BinaryExpr::NewAdd(Lit(Px::New(10)), Lit(Px::New(4)));
  const auto negated = UnaryExpr::NewMinus(Lit(Px::New(3)));
  ASSERT_TRUE(Build(Element(NodeExpr::kBlock, {Prop("width", sum), Prop("margin", negated)}), &built));

  const auto block = built.root->GetChildAt(0);
  const auto width = block->FindProperty("width");
  ASSERT_TRUE(width->IsConstant());
  EXPECT_EQ(static_cast<Px*>(width->constant)->GetValue(), 14);
  const auto margin = block->FindProperty("margin");
  ASSERT_TRUE(margin->IsConstant());
  EXPECT_EQ(static_cast<Px*>(margin->constant)->GetValue(), -3);
}

TEST(DomBuilder, FoldsNumbers) {
  Built built{};
  const auto product = BinaryExpr::NewMultiply(Lit(Int::New(2)), Lit(Int::New(3)));
  ASSERT_TRUE(Build(Element(NodeExpr::kBlock, {Prop("grow", product)}), &built));
  const auto grow = built.root->GetChildAt(0)->FindProperty("grow");
  ASSERT_TRUE(grow->IsConstant());
  EXPECT_EQ(static_cast<Int*>(grow->constant)->GetValue(), 6);
}

TEST(DomBuilder, UnitsAreNeverMixed) {
  Built built{};
  // 10px + 50%: the type analysis rejects it, so it isn't folded here
  const auto mixed = BinaryExpr::NewAdd(Lit(Px::New(10)), Lit(Pct::New(50)));
  ASSERT_TRUE(Build(Element(NodeExpr::kBlock, {Prop("width", mixed)}), &built));
  EXPECT_FALSE(built.root->GetChildAt(0)->FindProperty("width")->IsConstant());
}

TEST(DomBuilder, FollowsLetsOfConstants) {
  Built built{};
  const auto local = LocalVariable::New("w");
  const auto body = SeqExpr::New(ExprList{StoreLocalExpr::New(local, Lit(Px::New(8))),
                                          Element(NodeExpr::kBlock, {Prop("padding", LoadLocalExpr::New(local))})});
  ASSERT_TRUE(Build(body, &built));

  ASSERT_EQ(built.root->GetNumberOfChildren(), 2);
  EXPECT_TRUE(built.root->GetChildAt(0)->IsBind());
  const auto padding = built.root->GetChildAt(1)->FindProperty("padding");
  ASSERT_TRUE(padding->IsConstant());
  EXPECT_EQ(static_cast<Px*>(padding->constant)->GetValue(), 8);
}

TEST(DomBuilder, ResolvesLocalsFromOutside) {
  Built built{};
  const auto local = LocalVariable::New("w");
  dom::Builder builder(&built.tree, [&](LocalVariable* l) -> Object* {
    return l == local ? Px::New(2) : nullptr;
  });
  builder.Build(Element(NodeExpr::kBlock, {Prop("gap", LoadLocalExpr::New(local))}));
  ASSERT_TRUE(builder.Ok());
  EXPECT_TRUE(built.tree.GetRoot()->GetChildAt(0)->FindProperty("gap")->IsConstant());
}

TEST(DomBuilder, UnknownValuesStayExpressions) {
  Built built{};
  const auto local = LocalVariable::New("state");
  ASSERT_TRUE(Build(Element(NodeExpr::kText, {Prop("value", LoadLocalExpr::New(local))}), &built));
  const auto value = built.root->GetChildAt(0)->FindProperty("value");
  ASSERT_NE(value, nullptr);
  EXPECT_FALSE(value->IsConstant());
  EXPECT_NE(value->expr, nullptr);
}

TEST(DomBuilder, ControlFlowIsAnOpaqueHole) {
  Built built{};
  const auto local = LocalVariable::New("show");
  const auto cond = IfExpr::New(LoadLocalExpr::New(local), Element(NodeExpr::kText), nullptr);
  ASSERT_TRUE(Build(Element(NodeExpr::kFragment, {}, {cond}), &built));

  const auto fragment = built.root->GetChildAt(0);
  EXPECT_TRUE(fragment->IsFragment());
  ASSERT_EQ(fragment->GetNumberOfChildren(), 1);
  const auto hole = fragment->GetChildAt(0);
  EXPECT_TRUE(hole->IsDynamic());
  EXPECT_EQ(hole->GetExpr(), cond);
}

TEST(DomBuilder, TagsCascade) {
  Built built{};
  const auto inner = Element(NodeExpr::kText);
  const auto outer = Element(NodeExpr::kBlock, {}, {inner});
  const auto meta = new Directive{"meta", {Prop("tags", ListExpr::New(ExprList{VariantExpr::New("Button", nullptr)}))}};
  outer->SetDirectives({meta});
  ASSERT_TRUE(Build(outer, &built));

  const auto block = built.root->GetChildAt(0);
  ASSERT_EQ(block->GetTags().size(), 1);
  EXPECT_EQ(block->GetTags()[0], "Button");
  EXPECT_EQ(block->GetChildAt(0)->GetTags(), block->GetTags());
}

TEST(DomBuilder, UnknownPropertySuggests) {
  Built built{};
  EXPECT_FALSE(Build(Element(NodeExpr::kButton, {Prop("widht", Lit(Px::New(1)))}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_EQ(built.errors[0], "#button: unknown property 'widht' (did you mean 'width'?)");
}

TEST(DomBuilder, PropertyOfAnotherElement) {
  Built built{};
  EXPECT_FALSE(Build(Element(NodeExpr::kText, {Prop("source", Lit(String::New("a.png")))}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_EQ(built.errors[0], "#text: 'source' doesn't apply to #text");
}

TEST(DomBuilder, WrongTypeIsReported) {
  Built built{};
  EXPECT_FALSE(Build(Element(NodeExpr::kBlock, {Prop("width", Lit(Int::New(10)))}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_NE(built.errors[0].find("#block 'width': expected Px, Pct, Fr or \"auto\""), std::string::npos);
}

TEST(DomBuilder, FoldedValuesAreChecked) {
  Built built{};
  // -(4px) folds to a negative padding
  EXPECT_FALSE(Build(Element(NodeExpr::kBlock, {Prop("padding", UnaryExpr::NewMinus(Lit(Px::New(4))))}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_NE(built.errors[0].find("can't be negative"), std::string::npos);
}

TEST(DomBuilder, LeavesHaveNoChildren) {
  Built built{};
  EXPECT_FALSE(Build(Element(NodeExpr::kText, {}, {Element(NodeExpr::kText)}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_EQ(built.errors[0], "#text can't have children");
}

TEST(DomBuilder, DuplicatePropertyIsReported) {
  Built built{};
  EXPECT_FALSE(Build(Element(NodeExpr::kBlock, {Prop("gap", Lit(Px::New(1))), Prop("gap", Lit(Px::New(2)))}), &built));
  ASSERT_EQ(built.errors.size(), 1);
  EXPECT_EQ(built.errors[0], "#block: 'gap' is set more than once");
}

TEST(DomBuilder, ReportsEveryError) {
  Built built{};
  EXPECT_FALSE(
      Build(Element(NodeExpr::kBlock, {Prop("gap", Lit(Int::New(1))), Prop("nope", Lit(Int::New(1)))}), &built));
  EXPECT_EQ(built.errors.size(), 2);
}

TEST(DomPrinter, DumpsTheTree) {
  Built built{};
  const auto local = LocalVariable::New("n");
  const auto body =
      Element(NodeExpr::kButton, {Prop("radius", Lit(Px::New(4))), Prop("on-click", LoadLocalExpr::New(local))},
              {Element(NodeExpr::kText, {Prop("value", Lit(String::New("Go")))})});
  ASSERT_TRUE(Build(body, &built));

  const auto dump = Dump(&built);
  EXPECT_NE(dump.find("Fragment\n"), std::string::npos);
  EXPECT_NE(dump.find("\n  Button radius: Px(value=4) on-click: ~"), std::string::npos) << dump;
  EXPECT_NE(dump.find("\n    Text value: String(value=Go)\n"), std::string::npos) << dump;
}
