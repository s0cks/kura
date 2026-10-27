// The view of a module, from the DOM to instructions: builds a module by hand
// (no parser), lowers it, and looks at what came out.
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Only the parser may set a function's body and parameters, and this test has
// no parser: it opens `Function` up for the one include.
#define private public
#include "function.h"
#undef private

#include "backend/flow_graph.h"
#include "backend/flow_graph_builder.h"
#include "backend/gir.h"
#include "backend/type_analysis.h"
#include "frontend/expr.h"
#include "local_scope.h"
#include "module.h"
#include "object.h"
#include "property.h"
#include "type.h"

using namespace kura;
using namespace kura::expr;

namespace {
class TestNode : public NodeExpr {
 public:
  TestNode(const Kind kind, const PropertyList properties) :
    NodeExpr(kind, properties) {}

  using NodeExpr::Append;
};

class TestFunction : public Function {
 public:
  explicit TestFunction(const std::string& name) :
    Function(String::New(name)) {}
};

auto Prop(const std::string& name, Expr* value) -> StorePropertyExpr* {
  return StorePropertyExpr::New(Property::FindOrCreate(String::New(name), Type::NoneType()), value);
}

auto Lit(Object* value) -> Expr* {
  return LiteralExpr::New(value);
}

auto Element(const NodeExpr::Kind kind, const NodeExpr::PropertyList properties = {}, const ExprList children = {})
    -> NodeExpr* {
  const auto node = new TestNode(kind, properties);
  for (const auto child : children)
    node->Append(child);
  return node;
}

auto Text(const std::string& value) -> NodeExpr* {
  return Element(NodeExpr::kText, {Prop("value", Lit(String::New(value)))});
}

// `fn view(model) => <body>`
struct Program {
  Module* module = Module::New("test");
  TestFunction* view = new TestFunction("view");
  LocalVariable* model = LocalVariable::New("model");
  std::vector<std::string> errors{};
  std::stringstream dom_dump{};
  ModuleGraph* graphs = nullptr;

  // the body is set afterwards
  Program() {
    view->SetParameters({{model, nullptr}});
    module->functions.push_back(view);
  }

  explicit Program(Expr* body) :
    Program() {
    view->SetBody(SeqExpr::New(body));
  }

  // Lowers and type checks the view.
  auto Compile() -> bool {
    graphs = FlowGraphBuilder::BuildModuleGraph(module, &errors, &dom_dump);
    return graphs && TypeAnalysis::Run(module, graphs, &errors);
  }

  auto Graph() const -> FlowGraph* {
    return graphs->GetGraphFor(view);
  }

  // The instructions of the view, in the order of its blocks.
  auto Instructions() const -> std::vector<Instruction*> {
    std::vector<Instruction*> all{};
    for (const auto block : Graph()->GetReversePostorder()) {
      for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext())
        all.push_back(instr);
    }
    return all;
  }

  // The draw instructions of the view, as names.
  auto Draws() const -> std::vector<std::string> {
    std::vector<std::string> draws{};
    for (const auto instr : Instructions()) {
      if (instr->IsFillRect())
        draws.emplace_back("FillRect");
      else if (instr->IsDrawText())
        draws.emplace_back("DrawText");
    }
    return draws;
  }

  auto HasError(const std::string& text) const -> bool {
    for (const auto& error : errors) {
      if (error.find(text) != std::string::npos)
        return true;
    }
    return false;
  }
};
}  // namespace

TEST(DomLowering, TextDrawsItsValue) {
  Program program(Text("hello"));
  ASSERT_TRUE(program.Compile()) << (program.errors.empty() ? "" : program.errors[0]);
  EXPECT_EQ(program.Draws(), std::vector<std::string>{"DrawText"});

  for (const auto instr : program.Instructions()) {
    if (const auto draw = instr->AsDrawText()) {
      const auto constant = draw->GetValue()->GetDefinition()->AsConstant();
      ASSERT_NE(constant, nullptr);
      EXPECT_EQ(static_cast<String*>(constant->GetValue())->GetValue(), "hello");
    }
  }
}

TEST(DomLowering, TextWithoutAValueDrawsNothingInParticular) {
  Program program(Element(NodeExpr::kText));
  ASSERT_TRUE(program.Compile());
  EXPECT_EQ(program.Draws(), std::vector<std::string>{"DrawText"});
}

TEST(DomLowering, ButtonFillsThenDrawsItsChildren) {
  Program program(Element(NodeExpr::kButton, {}, {Text("a"), Text("b")}));
  ASSERT_TRUE(program.Compile());
  EXPECT_EQ(program.Draws(), (std::vector<std::string>{"FillRect", "DrawText", "DrawText"}));
}

TEST(DomLowering, ContainersRenderTheirChildren) {
  // a Block's children used to be built as objects and never drawn
  Program program(Element(NodeExpr::kBlock, {}, {Text("a"), Element(NodeExpr::kSurface, {}, {Text("b")})}));
  ASSERT_TRUE(program.Compile());
  EXPECT_EQ(program.Draws(), (std::vector<std::string>{"DrawText", "DrawText"}));
}

TEST(DomLowering, FragmentGroupsWithoutDrawing) {
  Program program(Element(NodeExpr::kFragment, {}, {Text("a"), Text("b")}));
  ASSERT_TRUE(program.Compile());
  EXPECT_EQ(program.Draws(), (std::vector<std::string>{"DrawText", "DrawText"}));
}

TEST(DomLowering, NoNodeObjectsAreBuilt) {
  Program program(Element(NodeExpr::kBlock, {Prop("gap", Lit(Px::New(2)))}, {Text("a")}));
  ASSERT_TRUE(program.Compile());
  for (const auto instr : program.Instructions())
    EXPECT_FALSE(instr->IsNewNode());
}

TEST(DomLowering, ConstantPropertiesEmitNothing) {
  Program program(Element(NodeExpr::kBlock, {Prop("gap", Lit(Px::New(2))), Prop("fill", Lit(Color::New("red")))}));
  ASSERT_TRUE(program.Compile());
  for (const auto instr : program.Instructions())
    EXPECT_FALSE(instr->IsConstant());
}

TEST(DomLowering, FoldsLetsIntoTheText) {
  const auto local = LocalVariable::New("title");
  Program program(SeqExpr::New(ExprList{StoreLocalExpr::New(local, Lit(String::New("Count"))),
                                        Element(NodeExpr::kText, {Prop("value", LoadLocalExpr::New(local))})}));
  ASSERT_TRUE(program.Compile());
  for (const auto instr : program.Instructions()) {
    if (const auto draw = instr->AsDrawText()) {
      const auto constant = draw->GetValue()->GetDefinition()->AsConstant();
      ASSERT_NE(constant, nullptr);
      EXPECT_EQ(static_cast<String*>(constant->GetValue())->GetValue(), "Count");
    }
  }
}

TEST(DomLowering, DynamicValuesAreEvaluated) {
  Program dynamic{};
  dynamic.view->SetBody(SeqExpr::New(
      Element(NodeExpr::kText, {Prop("value", LoadLocalExpr::New(dynamic.model))})));
  ASSERT_TRUE(dynamic.Compile()) << (dynamic.errors.empty() ? "" : dynamic.errors[0]);
  EXPECT_EQ(dynamic.Draws(), std::vector<std::string>{"DrawText"});
}

TEST(DomLowering, BranchesRenderEachArm) {
  Program program{};
  program.view->SetBody(
      SeqExpr::New(IfExpr::New(LoadLocalExpr::New(program.model), Text("yes"), Element(NodeExpr::kButton))));
  ASSERT_TRUE(program.Compile()) << (program.errors.empty() ? "" : program.errors[0]);
  // the arms are separate blocks, so only which draws there are is fixed
  auto draws = program.Draws();
  std::sort(draws.begin(), draws.end());
  EXPECT_EQ(draws, (std::vector<std::string>{"DrawText", "FillRect"}));
}

TEST(DomLowering, LoopsRenderTheirBody) {
  Program program{};
  const auto item = LocalVariable::New("item");
  program.view->SetBody(SeqExpr::New(new ListComprehensionExpr(
      LoadLocalExpr::New(program.model), Element(NodeExpr::kFragment, {}, {Text("a"), Text("b")}), item, "item")));
  ASSERT_TRUE(program.Compile()) << (program.errors.empty() ? "" : program.errors[0]);
  EXPECT_EQ(program.Draws(), (std::vector<std::string>{"DrawText", "DrawText"}));
}

TEST(DomLowering, ErrorsOfTheDomAreReported) {
  Program program(Element(NodeExpr::kBlock, {Prop("widht", Lit(Px::New(1)))}));
  EXPECT_FALSE(program.Compile());
  EXPECT_TRUE(program.HasError("unknown property 'widht' (did you mean 'width'?)"));
}

TEST(DomLowering, RuntimeValuesAreTypeChecked) {
  // `if model { 1 } else { 2 }` is an Int the compiler can't fold
  const auto unknown = [](Program* program) {
    return IfExpr::New(LoadLocalExpr::New(program->model), Lit(Int::New(1)), Lit(Int::New(2)));
  };

  Program wrong{};
  wrong.view->SetBody(SeqExpr::New(Element(NodeExpr::kBlock, {Prop("width", unknown(&wrong))})));
  EXPECT_FALSE(wrong.Compile());
  EXPECT_TRUE(wrong.HasError("#block 'width': expected Px, Pct, Fr or \"auto\", got Int"));

  Program fine{};
  fine.view->SetBody(SeqExpr::New(Element(NodeExpr::kBlock, {Prop("grow", unknown(&fine))})));
  EXPECT_TRUE(fine.Compile()) << (fine.errors.empty() ? "" : fine.errors[0]);
}

TEST(DomLowering, EventsAreNotEvaluated) {
  Program program(Element(NodeExpr::kButton, {Prop("on-click", VariantExpr::New("Tick"))}));
  ASSERT_TRUE(program.Compile());
  for (const auto instr : program.Instructions())
    EXPECT_FALSE(instr->IsNewVariant());
}

TEST(DomLowering, DumpsTheDomWhenAsked) {
  Program program(Element(NodeExpr::kButton, {Prop("radius", Lit(Px::New(4)))}, {Text("go")}));
  ASSERT_TRUE(program.Compile());
  const auto dump = program.dom_dump.str();
  EXPECT_NE(dump.find("dom of view:"), std::string::npos) << dump;
  EXPECT_NE(dump.find("Button radius: Px(value=4)"), std::string::npos) << dump;
  EXPECT_NE(dump.find("Text value: String(value=go)"), std::string::npos) << dump;
}
