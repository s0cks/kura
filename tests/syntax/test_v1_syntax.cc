// The syntax added after the first v1 grammar: casts and assertions, destructuring, pipes and composition,
// puns, interpolation, ranges and slices, comprehensions, `when`, `is`, guards, record updates, `??`, `?.`,
// element children and `set` rules.
#include <gtest/gtest.h>

#include "frontend/syntax/ast_printer.h"
#include "test_util.h"

namespace kura::syntax::test {
namespace {
using C = uint64_t;

auto Codes(const SyntaxTree& tree) -> std::vector<uint64_t> {
  std::vector<uint64_t> codes{};
  for (const auto& d : tree.GetDiagnostics())
    codes.push_back(d.code);
  return codes;
}

// The expression of `let x = <expr>`, which must parse without a single diagnostic.
auto Ok(const std::string& expr) -> std::string {
  const auto tree = Tree("let x = " + expr);
  EXPECT_TRUE(tree->IsValid()) << expr << "\n" << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  return ExprSexp(expr);
}

// The pattern of the first arm of `match v { <pattern> => 1 }`.
auto Pat(const std::string& pattern) -> std::string {
  const auto tree = Tree("let x = match v { " + pattern + " => 1 }");
  EXPECT_TRUE(tree->IsValid()) << pattern << "\n" << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* match = tree->GetRoot()->items[0]->As<Binding>()->value->As<MatchExpr>();
  return Sexp(match->arms[0]->As<MatchArm>()->pattern);
}

auto Diagnose(const std::string& text) -> std::vector<uint64_t> {
  return Codes(*Tree(text));
}
}  // namespace

// ---- casts, assertions ------------------------------------------------------------------------

TEST(V1Syntax, ACastIsACallOfTheTypeName) {
  EXPECT_EQ(Ok("Float(x)"), "(call Float x)");
  EXPECT_EQ(Ok("x |> Float()"), "(pipe x Float)");
}

TEST(V1Syntax, StaticAssertIsAnItemAndAStatement) {
  auto tree = Tree("static-assert(1 < 2, \"msg\")\nstatic-assert(true)\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  ASSERT_EQ(tree->GetRoot()->items.size(), 2U);
  const auto* first = tree->GetRoot()->items[0]->As<StaticAssert>();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(Sexp(first->condition), "(< 1 2)");
  EXPECT_EQ(Sexp(first->message), "\"msg\"");
  EXPECT_EQ(tree->GetRoot()->items[1]->As<StaticAssert>()->message, nullptr);

  tree = Tree("let x = { static-assert(true, \"m\") 1 }");
  ASSERT_TRUE(tree->IsValid());
  const auto* block = tree->GetRoot()->items[0]->As<Binding>()->value->As<Block>();
  ASSERT_EQ(block->statements.size(), 1U);
  EXPECT_TRUE(block->statements[0]->Is<StaticAssert>());
}

// ---- destructuring ----------------------------------------------------------------------------

TEST(V1Syntax, LetDestructuresARecord) {
  const auto tree = Tree("let { message, status: code, ...rest } = r");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* binding = tree->GetRoot()->items[0]->As<Binding>();
  ASSERT_NE(binding->pattern, nullptr);
  EXPECT_EQ(Sexp(binding->pattern), "{message, status: code, ...rest}");
  EXPECT_EQ(Sexp(binding->value), "r");
}

TEST(V1Syntax, RecordPatternsInMatch) {
  EXPECT_EQ(Pat("Point { x, y: 0 }"), "Point{x, y: 0}");
  EXPECT_EQ(Pat("{ x, y }"), "{x, y}");
  EXPECT_EQ(Pat("Point { x: 0, y }"), "Point{x: 0, y}");
  EXPECT_EQ(Pat("{ a: { b, c: 1 } }"), "{a: {b, c: 1}}");
  EXPECT_EQ(Pat("Point { x, ...others }"), "Point{x, ...others}");
  EXPECT_EQ(Pat("{ x, ... }"), "{x, ...}");
  EXPECT_EQ(Pat("Some({ x })"), "Some({x})");
  EXPECT_EQ(Pat("{}"), "{}");
}

TEST(V1Syntax, ArmsMayBeSeparatedByACommaOrByJustStartingTheNextOne) {
  const auto tree = Tree("let x = match v {\n  1 => \"a\"\n  2 => \"b\",\n  _ => \"c\"\n}");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  EXPECT_EQ(tree->GetRoot()->items[0]->As<Binding>()->value->As<MatchExpr>()->arms.size(), 3U);
}

TEST(V1Syntax, RangePatterns) {
  EXPECT_EQ(Pat("0..10"), "0..10");
  EXPECT_EQ(Pat("0..=10"), "0..=10");
  EXPECT_EQ(Pat("..5"), "..5");
  EXPECT_EQ(Pat("5.."), "5..");
  EXPECT_EQ(Pat("-10..-1"), "-10..-1");
  EXPECT_EQ(Pat("10px..20px"), "10px..20px");
}

TEST(V1Syntax, GuardsComeAfterThePattern) {
  const auto tree = Tree("let x = match v { n if n > 10 => 1, _ => 2 }");
  ASSERT_TRUE(tree->IsValid());
  const auto* arm = tree->GetRoot()->items[0]->As<Binding>()->value->As<MatchExpr>()->arms[0]->As<MatchArm>();
  EXPECT_EQ(Sexp(arm->pattern), "n");
  EXPECT_EQ(Sexp(arm->guard), "(> n 10)");
  EXPECT_EQ(Sexp(arm->body), "1");
}

TEST(V1Syntax, AParenthesizedGuardIsNotALambda) {
  const auto tree = Tree("let x = match v { n if (n > 1 || n < 0) => 1, _ => 2 }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* arm = tree->GetRoot()->items[0]->As<Binding>()->value->As<MatchExpr>()->arms[0]->As<MatchArm>();
  EXPECT_EQ(Sexp(arm->guard), "(paren (|| (> n 1) (< n 0)))");
  // a lambda inside the guard's own call arguments is still a lambda
  EXPECT_TRUE(Tree("let x = match v { n if any(xs, (a) => a > n) => 1, _ => 2 }")->IsValid());
}

// ---- pipes and composition --------------------------------------------------------------------

TEST(V1Syntax, PlaceholdersMarkWhereThePipedValueGoes) {
  EXPECT_EQ(Ok("10 |> add(_, 10) |> mul(2, _)"), "(pipe-stage (pipe-stage 10 (call add _ 10)) (call mul 2 _))");
  EXPECT_EQ(Ok("10 |> _ + 1"), "(pipe-stage 10 (+ _ 1))");
  EXPECT_EQ(Ok("4 |> mul(_, _)"), "(pipe-stage 4 (call mul _ _))");
  EXPECT_EQ(Ok("x |> f(g(_))"), "(pipe-stage x (call f (call g _)))");
}

TEST(V1Syntax, WithoutAPlaceholderThePipedValueIsTheFirstArgument) {
  EXPECT_EQ(Ok("10 |> add(10) |> mul(2)"), "(pipe (pipe 10 add 10) mul 2)");
  EXPECT_EQ(Ok("x |> math.sqrt()"), "(pipe x (. math sqrt))");
}

TEST(V1Syntax, APlaceholderAsWholeArgumentIsAPartialApplication) {
  EXPECT_EQ(Ok("add(_, 10)"), "(call add _ 10)");
  EXPECT_EQ(Diagnose("let x = f(_ + 1)"), (std::vector<C>{DiagnosticCode::kPlaceholderOutsideCall}));
  EXPECT_EQ(Diagnose("let x = _"), (std::vector<C>{DiagnosticCode::kPlaceholderOutsideCall}));
  EXPECT_EQ(Diagnose("let x = [_]"), (std::vector<C>{DiagnosticCode::kPlaceholderOutsideCall}));
}

TEST(V1Syntax, BackwardPipeIsRightAssociative) {
  EXPECT_EQ(Ok("add(_, 10) <| 200"), "(<| (call add _ 10) 200)");
  EXPECT_EQ(Ok("f <| g <| 1"), "(<| f (<| g 1))");
}

TEST(V1Syntax, MixingPipeDirectionsNeedsParentheses) {
  EXPECT_EQ(Diagnose("let x = 1 |> f(_) <| 2"), (std::vector<C>{DiagnosticCode::kMixedPipes}));
  EXPECT_EQ(Diagnose("let x = f <| 1 |> g(_)"), (std::vector<C>{DiagnosticCode::kMixedPipes}));
  EXPECT_TRUE(Tree("let x = (1 |> f(_)) |> g(_)")->IsValid());
}

TEST(V1Syntax, CompositionIsLooserThanArithmeticAndTighterThanPipes) {
  EXPECT_EQ(Ok("f >> g >> h"), "(>> (>> f g) h)");
  EXPECT_EQ(Ok("f << g"), "(<< f g)");
  EXPECT_EQ(Ok("f >> g |> h(_)"), "(pipe-stage (>> f g) (call h _))");
  EXPECT_EQ(Ok("a + 1 >> b"), "(>> (+ a 1) b)");
  // `>>` closing two type argument lists is still two `>`
  EXPECT_TRUE(Tree("let x: Seq<Seq<Int>> = []")->IsValid());
  // and a comparison is still a comparison
  EXPECT_EQ(Ok("a > b"), "(> a b)");
  EXPECT_EQ(Ok("a > -b"), "(> a (neg b))");
}

// ---- puns and spread --------------------------------------------------------------------------

TEST(V1Syntax, ATrailingCommaMakesARecordWithAPun) {
  const auto tree = Tree("let a = { width, }\nlet b = { width }\nlet c = { width, height: 4, depth }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* a = tree->GetRoot()->items[0]->As<Binding>()->value->As<RecordLit>();
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->entries.size(), 1U);
  const auto* pun = a->entries[0]->As<Arg>();
  EXPECT_EQ(pun->form, Arg::kPun);
  EXPECT_EQ(pun->name.text, "width");
  EXPECT_EQ(Sexp(pun->value), "width");
  // without the comma it is a block that returns `width`
  EXPECT_TRUE(tree->GetRoot()->items[1]->As<Binding>()->value->Is<Block>());
  const auto* c = tree->GetRoot()->items[2]->As<Binding>()->value->As<RecordLit>();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->entries.size(), 3U);
  EXPECT_EQ(c->entries[2]->As<Arg>()->form, Arg::kPun);
}

TEST(V1Syntax, ElementArgumentsPunWithATrailingComma) {
  const auto tree = Tree("fn v() => #button(width, height,)\nfn w() => #text(label)");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* button = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<UiElement>();
  ASSERT_EQ(button->props.size(), 2U);
  EXPECT_EQ(button->props[0]->As<Arg>()->form, Arg::kPun);
  EXPECT_EQ(button->props[1]->As<Arg>()->form, Arg::kPun);
  // a lone name with no comma is positional, as before
  const auto* text = tree->GetRoot()->items[1]->As<FnDecl>()->body->As<UiElement>();
  EXPECT_EQ(text->props[0]->As<Arg>()->form, Arg::kPositional);
}

TEST(V1Syntax, DirectiveArgumentsAreNeverPuns) {
  const auto tree = Tree("@pure(sqrt, floor)\nuse \"math.h\"\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* use = tree->GetRoot()->items[0]->As<UseDecl>();
  ASSERT_NE(use, nullptr);
  const auto* directive = use->directives[0]->As<Directive>();
  ASSERT_EQ(directive->args.size(), 2U);
  EXPECT_EQ(directive->args[0]->As<Arg>()->form, Arg::kPositional);
  EXPECT_EQ(directive->args[1]->As<Arg>()->form, Arg::kPositional);
}

TEST(V1Syntax, PositionalElementArgumentsComeLast) {
  EXPECT_TRUE(Tree("fn v() => #text(fill: red, \"hi\")")->IsValid());
  EXPECT_TRUE(Tree("fn v() => #text(...style, fill: red, \"hi\")")->IsValid());
  EXPECT_EQ(Diagnose("fn v() => #text(\"hi\", fill: red)"), (std::vector<C>{DiagnosticCode::kPositionalNotLast}));
  EXPECT_EQ(Diagnose("fn v() => #text(\"hi\", ...style)"), (std::vector<C>{DiagnosticCode::kPositionalNotLast}));
}

TEST(V1Syntax, SpreadKeepsItsPlaceSoTheLastEntryCanOverride) {
  const auto tree = Tree("fn v() => #button(...style, fill: red)");
  ASSERT_TRUE(tree->IsValid());
  const auto* button = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<UiElement>();
  ASSERT_EQ(button->props.size(), 2U);
  EXPECT_EQ(button->props[0]->As<Arg>()->form, Arg::kSpread);
  EXPECT_EQ(button->props[1]->As<Arg>()->form, Arg::kNamed);
}

// ---- interpolation ----------------------------------------------------------------------------

TEST(V1Syntax, StringsWithBracesBecomeTemplates) {
  EXPECT_EQ(Ok("\"{pos.x}, {pos.y}\""), "(template \"\" (. pos x) \", \" (. pos y) \"\")");
  EXPECT_EQ(Ok("\"a {1 + 2} b\""), "(template \"a \" (+ 1 2) \" b\")");
  EXPECT_EQ(Ok("\"no braces\""), "\"no braces\"");
  EXPECT_EQ(Ok("\"\\{literal\\}\""), "\"{literal}\"");
  EXPECT_EQ(Ok("\"lone } brace\""), "\"lone } brace\"");
  // a string inside an interpolation, with braces of its own
  EXPECT_EQ(Ok("\"x {f(\"y {z}\")} w\""), "(template \"x \" (call f (template \"y \" z \"\")) \" w\")");
}

TEST(V1Syntax, InterpolatedExpressionsKeepTheirPlaceInTheFile) {
  const std::string text = "let s = \"ab {name} cd\"";
  const auto tree = Tree(text);
  ASSERT_TRUE(tree->IsValid());
  const auto* tmpl = tree->GetRoot()->items[0]->As<Binding>()->value->As<StringTemplate>();
  ASSERT_NE(tmpl, nullptr);
  EXPECT_EQ(tree->GetText(tmpl->exprs[0]->span), "name");
}

TEST(V1Syntax, InterpolationProblemsAreReportedWhereTheyAre) {
  EXPECT_EQ(Diagnose("let s = \"{}\""), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("let s = \"{ }\""), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("let s = \"{a b}\""), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("let s = \"{a:.2}\""), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("let s = \"{a\n"), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("use \"a{b}.h\""), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  EXPECT_EQ(Diagnose("let x = match v { \"a{b}\" => 1 }"), (std::vector<C>{DiagnosticCode::kBadInterpolation}));
  const auto tree = Tree("let s = \"{a b}\"");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  EXPECT_EQ(tree->GetText(tree->GetDiagnostics().at(0).span), "b");
}

// ---- ranges and slices ------------------------------------------------------------------------

TEST(V1Syntax, Ranges) {
  EXPECT_EQ(Ok("0..10"), "(.. 0 10)");
  EXPECT_EQ(Ok("0..=10"), "(..= 0 10)");
  EXPECT_EQ(Ok("3.."), "(.. 3 _)");
  EXPECT_EQ(Ok("..5"), "(.. _ 5)");
  EXPECT_EQ(Ok("..=5"), "(..= _ 5)");
  EXPECT_EQ(Ok("a..b + 1"), "(.. a (+ b 1))");
  EXPECT_EQ(Ok("0..n || m"), "(.. 0 (|| n m))");
  EXPECT_EQ(Ok("0..-1"), "(.. 0 (neg 1))");
  EXPECT_EQ(Ok("0.5..1.5"), "(.. 0.5 1.5)");
}

TEST(V1Syntax, IndexingAndSlicing) {
  EXPECT_EQ(Ok("s[0]"), "([] s 0)");
  EXPECT_EQ(Ok("s[0..-1]"), "([] s (.. 0 (neg 1)))");
  EXPECT_EQ(Ok("s[1..]"), "([] s (.. 1 _))");
  EXPECT_EQ(Ok("s[..2]"), "([] s (.. _ 2))");
  EXPECT_EQ(Ok("s[..]"), "([] s (.. _ _))");
  EXPECT_EQ(Ok("s[1..][0]"), "([] ([] s (.. 1 _)) 0)");
  EXPECT_EQ(Ok("a.b[i].c"), "(. ([] (. a b) i) c)");
}

TEST(V1Syntax, ASeqOnTheNextLineIsNotAnIndex) {
  const auto tree = Tree("fn f() => {\n  let a = g\n  [1, 2]\n}");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* block = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<Block>();
  EXPECT_EQ(Sexp(block->statements[0]->As<Binding>()->value), "g");
  EXPECT_EQ(Sexp(block->result), "(seq 1 2)");
}

// ---- comprehensions and loops -----------------------------------------------------------------

TEST(V1Syntax, SeqComprehensions) {
  EXPECT_EQ(Ok("[ x + 1 for _, x in s ]"), "(comprehension _, x in s => (+ x 1))");
  EXPECT_EQ(Ok("[ x for _, x in s if x > 1 ]"), "(comprehension _, x in s if (> x 1) => x)");
  EXPECT_EQ(Ok("[ i for i in s ]"), "(comprehension i in s => i)");
  EXPECT_EQ(Ok("[ i * x for i, x in s ]"), "(comprehension i, x in s => (* i x))");
  EXPECT_EQ(Ok("[ [ y for _, y in r ] for _, r in rows ]"),
            "(comprehension _, r in rows => (comprehension _, y in r => y))");
  EXPECT_EQ(Ok("[\n  x + 1\n  for _, x in s\n]"), "(comprehension _, x in s => (+ x 1))");
  EXPECT_EQ(Ok("[ x for _, { x, y } in points ]"), "(comprehension _, {x, y} in points => x)");
}

TEST(V1Syntax, ListsAreStillLists) {
  EXPECT_EQ(Ok("[1, 2, 3]"), "(seq 1 2 3)");
  EXPECT_EQ(Ok("[]"), "(seq)");
  EXPECT_EQ(Ok("[ x ]"), "(seq x)");
  EXPECT_EQ(Diagnose("let x = [a for _, a in s, 2]"), (std::vector<C>{DiagnosticCode::kUnexpectedToken}));
  EXPECT_EQ(Diagnose("let x = [a for _, a in s fold t = 0 => t]"), (std::vector<C>{DiagnosticCode::kUnexpectedToken}));
}

TEST(V1Syntax, LoopBindingsFollowGo) {
  // one name is the index, two are the index and the element
  EXPECT_EQ(Ok("for i in xs => i"), "(for i in xs => i)");
  EXPECT_EQ(Ok("for i, x in xs => x"), "(for i, x in xs => x)");
  EXPECT_EQ(Ok("for _, x in xs => x"), "(for _, x in xs => x)");
  EXPECT_EQ(Ok("for i, _ in xs => i"), "(for i, _ in xs => i)");
  EXPECT_EQ(Ok("for _ in xs => 1"), "(for _ in xs => 1)");
  EXPECT_EQ(Ok("for i in 0..10 => i"), "(for i in (.. 0 10) => i)");
  EXPECT_EQ(Diagnose("let x = for a, b, c in xs => 1"), (std::vector<C>{DiagnosticCode::kBadForBinding}));
}

TEST(V1Syntax, AParenthesizedLoopGuardIsNotALambda) {
  EXPECT_EQ(Ok("for _, n in xs if (n > 1) => n"), "(for _, n in xs if (paren (> n 1)) => n)");
}

// ---- when, is ---------------------------------------------------------------------------------

TEST(V1Syntax, WhenIsAnIfWithoutAnElse) {
  EXPECT_EQ(Ok("when x > 5 { x }"), "(when (> x 5) (block 0 x))");
  EXPECT_EQ(Ok("when a { when b { c } }"), "(when a (block 0 (when b (block 0 c))))");
  EXPECT_EQ(Diagnose("let x = when a { 1 } else { 2 }"), (std::vector<C>{DiagnosticCode::kWhenWithElse}));
}

TEST(V1Syntax, IsTakesAPattern) {
  EXPECT_EQ(Ok("x is Float"), "(is x Float)");
  EXPECT_EQ(Ok("x is None"), "(is x None)");
  EXPECT_EQ(Ok("m is Click(_)"), "(is m Click(_))");
  EXPECT_EQ(Ok("p is Point { x: 0, y }"), "(is p Point{x: 0, y})");
  EXPECT_EQ(Ok("x is Int && y is Int"), "(&& (is x Int) (is y Int))");
  EXPECT_EQ(Ok("x is Int == true"), "(== (is x Int) true)");
  EXPECT_EQ(Ok("n is 0..10"), "(is n 0..10)");
}

TEST(V1Syntax, AnIfConditionEndsAtItsBlockNotAtARecordPattern) {
  // the `{` after `Point` starts the block, so the condition is `x is Point`
  const auto tree = Tree("let a = if x is Point { 1 } else { 2 }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* branch = tree->GetRoot()->items[0]->As<Binding>()->value->As<IfExpr>();
  EXPECT_EQ(Sexp(branch->condition), "(is x Point)");
  // parentheses bring record patterns back
  const auto wrapped = Tree("let a = if (x is Point { x, y }) { 1 } else { 2 }");
  ASSERT_TRUE(wrapped->IsValid()) << RenderToString(wrapped->GetSource(), wrapped->GetDiagnostics());
  EXPECT_EQ(Sexp(wrapped->GetRoot()->items[0]->As<Binding>()->value->As<IfExpr>()->condition),
            "(paren (is x Point{x, y}))");
  // `when` and `match` read their conditions the same way
  EXPECT_EQ(Ok("when x is Point { 1 }"), "(when (is x Point) (block 0 1))");
}

// ---- record update ----------------------------------------------------------------------------

TEST(V1Syntax, RecordUpdates) {
  EXPECT_EQ(Ok("{ p | x: p.x + 1 }"), "(update p x:(+ (. p x) 1))");
  EXPECT_EQ(Ok("{ p | x += 1, y -= 2 }"), "(update p x+=1 y-=2)");
  EXPECT_EQ(Ok("{ p | x *= 2, y /= 2, z %= 3 }"), "(update p x*=2 y/=2 z%=3)");
  EXPECT_EQ(Ok("{ m | player.hp -= 1, player.pos.x += 3 }"), "(update m player.hp-=1 player.pos.x+=3)");
  EXPECT_EQ(Ok("{ m.player | hp: 0 }"), "(update (. m player) hp:0)");
  EXPECT_EQ(Ok("{ p |\n  x += 1,\n}"), "(update p x+=1)");
}

TEST(V1Syntax, RecordUpdateDoesNotGetInTheWayOfRecordsAndBlocks) {
  EXPECT_EQ(Ok("{ ...p, x: 0 }"), "(record ...p x:0)");
  EXPECT_EQ(Ok("{ x: 1 }"), "(record x:1)");
  EXPECT_EQ(Ok("{ a || b }"), "(block 0 (|| a b))");
  EXPECT_EQ(Diagnose("let x = { p | }"), (std::vector<C>{DiagnosticCode::kBadRecordUpdate}));
  EXPECT_EQ(Diagnose("let x = { p | x }"), (std::vector<C>{DiagnosticCode::kBadRecordUpdate}));
  EXPECT_EQ(Diagnose("let x = { p | x = 1 }"), (std::vector<C>{DiagnosticCode::kBadRecordUpdate}));
}

// ---- ?? and ?. --------------------------------------------------------------------------------

TEST(V1Syntax, Coalesce) {
  EXPECT_EQ(Ok("a ?? b"), "(?? a b)");
  EXPECT_EQ(Ok("a ?? b ?? c"), "(?? a (?? b c))");
  EXPECT_EQ(Ok("a ?? b || c"), "(?? a (|| b c))");
  EXPECT_EQ(Ok("a || b ?? c"), "(?? (|| a b) c)");
  EXPECT_EQ(Ok("a + 1 ?? 2"), "(?? (+ a 1) 2)");
}

TEST(V1Syntax, OptionalChaining) {
  EXPECT_EQ(Ok("e?.pos?.x"), "(?. (?. e pos) x)");
  EXPECT_EQ(Ok("f?.(1, 2)"), "(call? f 1 2)");
  EXPECT_EQ(Ok("xs?.[0]"), "(?[] xs 0)");
  EXPECT_EQ(Ok("a?.b?.[0]?.c?.(1)"), "(call? (?. (?[] (?. a b) 0) c) 1)");
  EXPECT_EQ(Ok("e?.pos?.x ?? 0.0"), "(?? (?. (?. e pos) x) 0.0)");
}

// ---- elements, children and set rules ---------------------------------------------------------

TEST(V1Syntax, StringsSpacersAndDynamicContentAreChildren) {
  const auto tree = Tree(
      "fn v() => #block {\n"
      "  \"Hello\"\n"
      "  \"World\"\n"
      "  1fr\n"
      "  10px\n"
      "  #v(1fr)\n"
      "  when w { #text(\"a\") }\n"
      "  if w { #a() } else { #b() }\n"
      "  for _, i in xs => #text(i)\n"
      "  match w { _ => #a() }\n"
      "  helper(1, 2)\n"
      "}");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* block = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<UiElement>();
  std::vector<Node::Kind> kinds{};
  for (const auto* child : block->children)
    kinds.push_back(child->GetKind());
  EXPECT_EQ(kinds, (std::vector<Node::Kind>{Node::kStringLit, Node::kStringLit, Node::kUnitLit, Node::kUnitLit,
                                            Node::kUiElement, Node::kWhenExpr, Node::kIfExpr, Node::kForExpr,
                                            Node::kMatchExpr, Node::kCall}));
}

TEST(V1Syntax, SetRulesAreItemsStatementsChildrenAndFunctionPrefixes) {
  auto tree = Tree("set button(fill: black)\nset text(fill: white, size: 14px)\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  ASSERT_EQ(tree->GetRoot()->items.size(), 2U);
  const auto* rule = tree->GetRoot()->items[1]->As<SetRule>();
  ASSERT_NE(rule, nullptr);
  EXPECT_EQ(rule->tag.text, "text");
  EXPECT_EQ(rule->props.size(), 2U);

  // before an unbraced body, any number of them
  tree = Tree("fn view() =>\n  set button(fill: green)\n  set text(fill: red)\n  #fragment { #button() }\n");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* fn = tree->GetRoot()->items[0]->As<FnDecl>();
  EXPECT_EQ(fn->set_rules.size(), 2U);
  EXPECT_TRUE(fn->body->Is<UiElement>());

  // in a block, and among an element's children
  tree = Tree("fn f() => { set text(fill: red) #text(\"a\") }\nfn g() => #block { set text(fill: red) #text(\"a\") }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  EXPECT_TRUE(tree->GetRoot()->items[0]->As<FnDecl>()->body->As<Block>()->statements[0]->Is<SetRule>());
  EXPECT_TRUE(tree->GetRoot()->items[1]->As<FnDecl>()->body->As<UiElement>()->children[0]->Is<SetRule>());
}

TEST(V1Syntax, ALetNeedsABracedBody) {
  // `let` can't follow the `set` rules of an unbraced body
  EXPECT_FALSE(Tree("fn v() =>\n  set text(fill: red)\n  let a = 1\n  #text(a)\n")->IsValid());
  EXPECT_TRUE(Tree("fn v() => {\n  set text(fill: red)\n  let a = 1\n  #text(a)\n}\n")->IsValid());
}

TEST(V1Syntax, SetIsAnOrdinaryNameExceptWhereASetRuleStarts) {
  EXPECT_TRUE(Tree("fn set(n: Int): Int => n\nlet a = set(1)\nlet b = { let set = 2 set }")->IsValid());
  EXPECT_TRUE(Tree("fn v() => #block { set(1) }")->IsValid());
  EXPECT_TRUE(Tree("let set = 1\nset = 2")->IsValid());
  // a name after it is what makes it a rule
  const auto tree = Tree("fn v() => #block { set text(fill: red) set(2) }");
  ASSERT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  const auto* block = tree->GetRoot()->items[0]->As<FnDecl>()->body->As<UiElement>();
  ASSERT_EQ(block->children.size(), 2U);
  EXPECT_TRUE(block->children[0]->Is<SetRule>());
  EXPECT_TRUE(block->children[1]->Is<Call>());
}

TEST(V1Syntax, SetRuleProblems) {
  EXPECT_FALSE(Tree("set button")->IsValid());
  EXPECT_FALSE(Tree("set (fill: red)")->IsValid());
  EXPECT_FALSE(Tree("@pure set button(fill: red)")->IsValid());
}

// ---- lexical details --------------------------------------------------------------------------

TEST(V1Syntax, AnUnspacedSubtractionOfADigitSuggestsTheSpaces) {
  const auto tree = Tree("let a = x-1");
  ASSERT_EQ(tree->GetDiagnostics().size(), 1U);
  const auto& d = tree->GetDiagnostics().at(0);
  EXPECT_EQ(d.code, DiagnosticCode::kBinaryMinusSpacing);
  EXPECT_NE(RenderToString(tree->GetSource(), tree->GetDiagnostics()).find("did you mean `x - 1`"), std::string::npos);
  // a kebab-case name with digits in it is still a name
  EXPECT_TRUE(Tree("let pos-2d-to-3d = 1\nlet a = pos-2d-to-3d")->IsValid());
}
}  // namespace kura::syntax::test
