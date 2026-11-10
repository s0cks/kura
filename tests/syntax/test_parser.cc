#include <gtest/gtest.h>

#include "frontend/syntax/ast_printer.h"
#include "test_util.h"

namespace kura::syntax::test {
// ---- expressions ------------------------------------------------------------

TEST(ParserExprTest, ArithmeticPrecedenceAndAssociativity) {
  EXPECT_EQ(ExprSexp("a + b * c"), "(+ a (* b c))");
  EXPECT_EQ(ExprSexp("a * b + c"), "(+ (* a b) c)");
  EXPECT_EQ(ExprSexp("a - b - c"), "(- (- a b) c)");
  EXPECT_EQ(ExprSexp("a / b * c"), "(* (/ a b) c)");
  EXPECT_EQ(ExprSexp("(a + b) * c"), "(* (paren (+ a b)) c)");
}

TEST(ParserExprTest, ComparisonAndLogicPrecedence) {
  EXPECT_EQ(ExprSexp("a || b && c"), "(|| a (&& b c))");
  EXPECT_EQ(ExprSexp("a && b == c"), "(&& a (== b c))");
  EXPECT_EQ(ExprSexp("a == b < c"), "(== a (< b c))");
  EXPECT_EQ(ExprSexp("a < b + c"), "(< a (+ b c))");
  EXPECT_EQ(ExprSexp("a != b || c >= d"), "(|| (!= a b) (>= c d))");
}

TEST(ParserExprTest, UnaryBindsTighterThanMultiplyLooserThanPostfix) {
  EXPECT_EQ(ExprSexp("-a.b"), "(neg (. a b))");
  EXPECT_EQ(ExprSexp("-f(x)"), "(neg (call f x))");
  EXPECT_EQ(ExprSexp("-a * b"), "(* (neg a) b)");
  EXPECT_EQ(ExprSexp("!a && b"), "(&& (not a) b)");
  EXPECT_EQ(ExprSexp("a * -b"), "(* a (neg b))");
  EXPECT_EQ(ExprSexp("a - -b"), "(- a (neg b))");
  EXPECT_EQ(ExprSexp("!!a"), "(not (not a))");
}

TEST(ParserExprTest, PostfixChains) {
  EXPECT_EQ(ExprSexp("f(x).y(z)"), "(call (. (call f x) y) z)");
  EXPECT_EQ(ExprSexp("a.b.c"), "(. (. a b) c)");
  EXPECT_EQ(ExprSexp("f(1, 2,)"), "(call f 1 2)");
  EXPECT_EQ(ExprSexp("f()"), "(call f)");
  EXPECT_EQ(ExprSexp("Click(p)"), "(call Click p)");
}

TEST(ParserExprTest, KeywordsAndColorsAreFineAsMemberNames) {
  EXPECT_EQ(ExprSexp("x.type"), "(. x type)");
  EXPECT_EQ(ExprSexp("x.fn"), "(. x fn)");
  EXPECT_EQ(ExprSexp("x.None"), "(. x None)");
  EXPECT_EQ(ExprSexp("x.red"), "(. x red)");
  EXPECT_EQ(ExprSexp("x.`odd name`"), "(. x `odd name`)");
}

TEST(ParserExprTest, PipesAreLowestPrecedenceAndLeftAssociative) {
  EXPECT_EQ(ExprSexp("a + b |> f(c) |> g()"), "(pipe (pipe (+ a b) f c) g)");
  EXPECT_EQ(ExprSexp("x |> m.f(1)"), "(pipe x (. m f) 1)");
  EXPECT_EQ(ExprSexp("x |> Some()"), "(pipe x Some)");
  EXPECT_EQ(ExprSexp("x |> (f)(1)"), "(pipe x (paren f) 1)");
}

TEST(ParserExprTest, LambdaVersusParenthesis) {
  EXPECT_EQ(ExprSexp("(a, b) => a + b"), "(lambda a b => (+ a b))");
  EXPECT_EQ(ExprSexp("() => 1"), "(lambda => 1)");
  EXPECT_EQ(ExprSexp("(a,) => a"), "(lambda a => a)");
  EXPECT_EQ(ExprSexp("(a)"), "(paren a)");
  EXPECT_EQ(ExprSexp("((a))"), "(paren (paren a))");
  EXPECT_EQ(ExprSexp("f((x) => x)"), "(call f (lambda x => x))");
}

TEST(ParserExprTest, RecordVersusBlock) {
  EXPECT_EQ(ExprSexp("{}"), "(record)");
  EXPECT_EQ(ExprSexp("{ a: 1, b: 2, }"), "(record a:1 b:2)");
  EXPECT_EQ(ExprSexp("{ ...base, a: 1 }"), "(record ...base a:1)");
  EXPECT_EQ(ExprSexp("{ a }"), "(block 0 a)");
  EXPECT_EQ(ExprSexp("{ let y = 1 y }"), "(block 1 y)");
  EXPECT_EQ(ExprSexp("{ let y = 1 }"), "(block 1 -)");
  EXPECT_EQ(ExprSexp("{ y = 1 y }"), "(block 1 y)");
}

TEST(ParserExprTest, SeqAndLiterals) {
  EXPECT_EQ(ExprSexp("[1, 2, 3,]"), "(seq 1 2 3)");
  EXPECT_EQ(ExprSexp("[]"), "(seq)");
  EXPECT_EQ(ExprSexp("10px + 50% + 1fr + blue"), "(+ (+ (+ 10px 50%) 1fr) blue)");
  EXPECT_EQ(ExprSexp("None"), "None");
  EXPECT_EQ(ExprSexp("true && false"), "(&& true false)");
  EXPECT_EQ(ExprSexp("\"a\\n\\\"b\\\\\""), "\"a\n\"b\\\"");
}

TEST(ParserExprTest, IfElseChain) {
  EXPECT_EQ(ExprSexp("if a { 1 } else if b { 2 } else { 3 }"), "(if a (block 0 1) (if b (block 0 2) (block 0 3)))");
  EXPECT_EQ(ExprSexp("if a == b { } else { }"), "(if (== a b) (block 0 -) (block 0 -))");
}

TEST(ParserExprTest, MatchPatterns) {
  const auto tree = Tree(
      "let x = match m {\n"
      "  _ => 0,\n"
      "  y => 1,\n"
      "  None => 2,\n"
      "  Click(p) => 3,\n"
      "  Tick => 4,\n"
      "  -1 => 5,\n"
      "  10px => 6,\n"
      "  red => 7,\n"
      "  \"s\" => 8,\n"
      "  true => 9,\n"
      "}");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* match = tree->GetRoot()->items[0]->As<Binding>()->value->As<MatchExpr>();
  ASSERT_NE(match, nullptr);
  ASSERT_EQ(match->arms.size(), 10U);
  const auto kind = [&](const size_t idx) {
    return match->arms[idx]->As<MatchArm>()->pattern->GetKind();
  };
  EXPECT_EQ(kind(0), Node::kWildcardPattern);
  EXPECT_EQ(kind(1), Node::kBindingPattern);
  EXPECT_EQ(kind(2), Node::kNonePattern);
  EXPECT_EQ(kind(3), Node::kVariantPattern);
  EXPECT_NE(match->arms[3]->As<MatchArm>()->pattern->As<VariantPattern>()->payload, nullptr);
  EXPECT_EQ(match->arms[4]->As<MatchArm>()->pattern->As<VariantPattern>()->payload, nullptr);
  EXPECT_TRUE(match->arms[5]->As<MatchArm>()->pattern->As<LiteralPattern>()->negated);
  for (size_t idx = 5; idx < 10; idx++)
    EXPECT_EQ(kind(idx), Node::kLiteralPattern);
}

TEST(ParserExprTest, ForLoops) {
  auto tree = Tree("let a = for _, n in xs if n > 1 fold acc = 0 => acc + n");
  ASSERT_TRUE(tree->IsValid());
  const auto* loop = tree->GetRoot()->items[0]->As<Binding>()->value->As<ForExpr>();
  ASSERT_NE(loop, nullptr);
  EXPECT_EQ(loop->first.text, "_");
  ASSERT_NE(loop->second, nullptr);
  EXPECT_EQ(loop->second->As<BindingPattern>()->name.text, "n");
  EXPECT_NE(loop->guard, nullptr);
  EXPECT_TRUE(loop->has_fold);
  EXPECT_EQ(loop->accumulator.text, "acc");
  EXPECT_EQ(Sexp(loop->body), "(+ acc n)");

  tree = Tree("let a = for _, n in xs => n * 2");
  ASSERT_TRUE(tree->IsValid());
  loop = tree->GetRoot()->items[0]->As<Binding>()->value->As<ForExpr>();
  EXPECT_EQ(loop->guard, nullptr);
  EXPECT_FALSE(loop->has_fold);
}

TEST(ParserExprTest, UiElements) {
  const auto tree = Tree(
      "fn view(): Content =>\n"
      "  @flip #row(gap: 4px, ...extra, 1) {\n"
      "    let w = 10px\n"
      "    @hidden #text(\"hi\")\n"
      "    #spacer\n"
      "  }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* row = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<UiElement>();
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row->tag.text, "row");
  EXPECT_EQ(row->directives.size(), 1U);
  ASSERT_EQ(row->props.size(), 3U);
  EXPECT_EQ(row->props[0]->As<Arg>()->form, Arg::kNamed);
  EXPECT_EQ(row->props[1]->As<Arg>()->form, Arg::kSpread);
  EXPECT_EQ(row->props[2]->As<Arg>()->form, Arg::kPositional);
  ASSERT_EQ(row->children.size(), 3U);
  EXPECT_TRUE(row->children[0]->Is<Binding>());
  EXPECT_EQ(row->children[1]->As<UiElement>()->directives.size(), 1U);
  EXPECT_FALSE(row->children[2]->As<UiElement>()->has_props);
}

// ---- declarations -----------------------------------------------------------

TEST(ParserDeclTest, TypeDeclarations) {
  const auto tree = Tree(
      "type Msg = Click(Position) | Tick | None\n"
      "type Box<T, U> = { value: T, other?: U }\n"
      "type Handler = (Int, Float) => None\n"
      "type Nested = Seq<Seq<Px>>\n"
      "type Foreign = rl.Vector2 | Other.Thing<Int>\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto& items = tree->GetRoot()->items;
  ASSERT_EQ(items.size(), 5U);
  const auto* msg = items[0]->As<TypeDecl>();
  ASSERT_TRUE(msg->type->Is<UnionType>());
  EXPECT_EQ(msg->type->As<UnionType>()->members.size(), 3U);
  EXPECT_TRUE(msg->type->As<UnionType>()->members[0]->Is<VariantType>());
  EXPECT_TRUE(msg->type->As<UnionType>()->members[2]->Is<NoneType>());
  const auto* box = items[1]->As<TypeDecl>();
  ASSERT_EQ(box->type_params.size(), 2U);
  const auto* record = box->type->As<RecordType>();
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->fields[0]->As<Field>()->optional);
  EXPECT_TRUE(record->fields[1]->As<Field>()->optional);
  EXPECT_TRUE(items[2]->As<TypeDecl>()->type->Is<FunctionType>());
  const auto* nested = items[3]->As<TypeDecl>()->type->As<NamedType>();
  ASSERT_EQ(nested->args.size(), 1U);
  EXPECT_EQ(nested->args[0]->As<NamedType>()->args.size(), 1U);
  const auto* foreign = items[4]->As<TypeDecl>()->type->As<UnionType>();
  EXPECT_TRUE(foreign->members[0]->Is<QualifiedType>());
  EXPECT_EQ(foreign->members[1]->As<QualifiedType>()->qualifier.text, "Other");
}

TEST(ParserDeclTest, FunctionsExternsAndUses) {
  const auto tree = Tree(
      "use \"math.h\"\n"
      "use \"math.h\" as m\n"
      "@pure extern \"sqrtf\" fn sqrt(x: Float): Float\n"
      "extern fn printf(fmt: Ptr, ...): Int\n"
      "extern fn `fn`(x: Int): Int => x\n"
      "fn add(a: Int, b: Int,): Int => a + b\n"
      "fn nothing() => 1\n"
      "let n = 1\n"
      "n = 2\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto& items = tree->GetRoot()->items;
  ASSERT_EQ(items.size(), 9U);
  EXPECT_FALSE(items[0]->As<UseDecl>()->has_alias);
  EXPECT_EQ(items[1]->As<UseDecl>()->alias.text, "m");
  EXPECT_EQ(items[0]->As<UseDecl>()->header, "math.h");
  const auto* sqrt = items[2]->As<ExternDecl>();
  EXPECT_EQ(sqrt->symbol, "sqrtf");
  EXPECT_EQ(sqrt->c_name.text, "sqrt");
  EXPECT_EQ(sqrt->directives.size(), 1U);
  EXPECT_TRUE(items[3]->As<ExternDecl>()->variadic);
  EXPECT_EQ(items[3]->As<ExternDecl>()->params.size(), 1U);
  EXPECT_TRUE(items[4]->As<ExternDecl>()->c_name.IsRaw());
  EXPECT_EQ(items[4]->As<ExternDecl>()->c_name.GetValue(), "fn");
  EXPECT_NE(items[4]->As<ExternDecl>()->body, nullptr);
  EXPECT_EQ(items[5]->As<FnDecl>()->params.size(), 2U);
  EXPECT_EQ(items[6]->As<FnDecl>()->result, nullptr);
  EXPECT_TRUE(items[7]->As<Binding>()->is_let);
  EXPECT_FALSE(items[8]->As<Binding>()->is_let);
}

TEST(ParserDeclTest, DirectivesAttachToTheNextItemAndKeepArgs) {
  const auto tree = Tree("@a @b(1, k: 2) let x = 1");
  ASSERT_TRUE(tree->IsValid());
  const auto* binding = tree->GetRoot()->items[0]->As<Binding>();
  ASSERT_EQ(binding->directives.size(), 2U);
  EXPECT_EQ(binding->directives[0]->As<Directive>()->name.text, "a");
  EXPECT_FALSE(binding->directives[0]->As<Directive>()->has_parens);
  EXPECT_EQ(binding->directives[1]->As<Directive>()->args.size(), 2U);
}

// ---- spans ------------------------------------------------------------------

TEST(ParserSpanTest, NodesCoverExactlyTheirText) {
  const auto tree = Tree("fn f(x: Int): Int => g(x.y, 1 + 2)\n");
  ASSERT_TRUE(tree->IsValid());
  std::vector<const Node*> nodes{};
  CollectNodes(tree->GetRoot(), nodes);
  const auto text_of = [&](const Node* n) { return std::string(tree->GetText(n)); };
  bool saw_call = false;
  bool saw_member = false;
  bool saw_binary = false;
  for (const auto* node : nodes) {
    if (node->Is<Call>()) {
      saw_call = true;
      EXPECT_EQ(text_of(node), "g(x.y, 1 + 2)");
    } else if (node->Is<Member>()) {
      saw_member = true;
      EXPECT_EQ(text_of(node), "x.y");
      EXPECT_EQ(tree->GetText(node->As<Member>()->member.span), "y");
    } else if (node->Is<Binary>()) {
      saw_binary = true;
      EXPECT_EQ(text_of(node), "1 + 2");
      EXPECT_EQ(tree->GetText(node->As<Binary>()->op_span), "+");
    } else if (node->Is<FnDecl>()) {
      EXPECT_EQ(text_of(node), "fn f(x: Int): Int => g(x.y, 1 + 2)");
      EXPECT_EQ(tree->GetText(node->As<FnDecl>()->name.span), "f");
    } else if (node->Is<Param>()) {
      EXPECT_EQ(text_of(node), "x: Int");
    }
  }
  EXPECT_TRUE(saw_call && saw_member && saw_binary);
}

TEST(ParserSpanTest, DirectivesAreInTheSpanOfTheirItem) {
  const auto tree = Tree("@pure\nfn f() => 1");
  ASSERT_TRUE(tree->IsValid());
  const auto* fn = tree->GetRoot()->items[0]->As<FnDecl>();
  EXPECT_EQ(tree->GetText(fn), "@pure\nfn f() => 1");
  EXPECT_EQ(tree->GetText(fn->directives[0]->As<Directive>()->name.span), "pure");
}

TEST(ParserSpanTest, EveryNodeIsInsideItsParent) {
  const auto tree = Tree(
      "type T = A | B(Int)\n"
      "fn f(x: Int): Int => match x { 1 => { a: 2 }, _ => [3] }\n"
      "fn g() => #a(k: 1) { #b @c #d }\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  std::vector<const Node*> stack{tree->GetRoot()};
  while (!stack.empty()) {
    const auto* node = stack.back();
    stack.pop_back();
    node->ForEachChild([&](const Node* child) {
      EXPECT_GE(child->span.begin, node->span.begin) << node->GetKindName() << " > " << child->GetKindName();
      EXPECT_LE(child->span.end, node->span.end) << node->GetKindName() << " > " << child->GetKindName();
      stack.push_back(child);
    });
  }
}

TEST(ParserSpanTest, LinesAndColumns) {
  const SourceFile file("t", "ab\ncde\n\nf");
  EXPECT_EQ(file.GetPosition(0), (Position{0, 0}));
  EXPECT_EQ(file.GetPosition(4), (Position{1, 1}));
  EXPECT_EQ(file.GetPosition(7), (Position{2, 0}));
  EXPECT_EQ(file.GetPosition(8), (Position{3, 0}));
  EXPECT_EQ(file.GetPosition(999), (Position{3, 1}));
  EXPECT_EQ(file.GetOffset(Position{1, 2}), 5U);
  EXPECT_EQ(file.GetNumberOfLines(), 4U);
  EXPECT_EQ(file.GetText(file.GetLineSpan(1)), "cde");
  // `é` is two bytes in UTF-8 but one UTF-16 unit; an emoji is two units
  const SourceFile unicode("u", "\xC3\xA9x\xF0\x9F\x98\x80y");
  EXPECT_EQ(unicode.GetUtf16Column(0), 0U);
  EXPECT_EQ(unicode.GetUtf16Column(2), 1U);
  EXPECT_EQ(unicode.GetUtf16Column(3), 2U);
  EXPECT_EQ(unicode.GetUtf16Column(7), 4U);
}

TEST(ParserSpanTest, FindNodeAtFindsTheInnermostNode) {
  const auto tree = Tree("let total = price * count\n");
  ASSERT_TRUE(tree->IsValid());
  const auto offset = static_cast<uint32_t>(std::string("let total = pri").size());
  const auto* node = FindNodeAt(tree->GetRoot(), offset);
  ASSERT_NE(node, nullptr);
  ASSERT_TRUE(node->Is<VarRef>());
  EXPECT_EQ(node->As<VarRef>()->name.text, "price");
  const auto path = FindPathAt(tree->GetRoot(), offset);
  ASSERT_GE(path.size(), 4U);  // Module > Binding > Binary > VarRef
  EXPECT_TRUE(path[0]->Is<Module>());
  EXPECT_TRUE(path[1]->Is<Binding>());
  EXPECT_TRUE(path[2]->Is<Binary>());
  // a cursor right after the last character still belongs to that identifier
  const auto end_offset = static_cast<uint32_t>(std::string("let total = price * count").size());
  EXPECT_EQ(FindNodeAt(tree->GetRoot(), end_offset)->As<VarRef>()->name.text, "count");
  EXPECT_EQ(FindNodeAt(tree->GetRoot(), 9999), nullptr);
}
}  // namespace kura::syntax::test
