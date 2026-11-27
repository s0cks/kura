#include <gtest/gtest.h>

#include <algorithm>

#include "lsp/binder.h"
#include "lsp/completion.h"
#include "lsp/diagnostic.h"
#include "lsp/formatting.h"
#include "lsp/hover.h"
#include "lsp/navigation.h"
#include "lsp/outline.h"
#include "lsp/signature_help.h"
#include "lsp/uri.h"

namespace kura::lsp {
namespace {
// `§` marks the cursor.
struct Marked {
  std::string text{};
  uint32_t offset = 0;
};

auto Mark(std::string text) -> Marked {
  const auto at = text.find("§");
  EXPECT_NE(at, std::string::npos);
  text.erase(at, std::string("§").size());
  return {std::move(text), static_cast<uint32_t>(at)};
}

constexpr auto kProgram = R"(type Message = Increment | Set(Int) | Resized(Size)

type State = {
  value: Int,
  width: Float,
}

fn update(msg: Message, state: State): State =>
  match msg {
    Increment => { state | value += 1 }
    Set(n) => { state | value: n }
    Resized(size) => { state | width: size.width }
  }

fn view(state: State): Node => {
  let doubled = state.value
  #block(direction: "column"){
    #button(on-click: Increment){
      #text("value: {state.value}")
    }
  }
}
)";

auto Open(const std::string& text) -> std::unique_ptr<Document> {
  auto doc = std::make_unique<Document>("file:///test.kui", text);
  doc->EnsureTree();
  return doc;
}

auto At(const std::string& text, const std::string& needle, const int nth = 0, const int delta = 0) -> Position {
  size_t idx = std::string::npos;
  for (int i = 0; i <= nth; i++)
    idx = text.find(needle, idx == std::string::npos ? 0 : idx + 1);
  EXPECT_NE(idx, std::string::npos) << needle;
  idx += delta;
  const auto line = std::count(text.begin(), text.begin() + static_cast<long>(idx), '\n');
  const auto col = idx - (text.rfind('\n', idx - 1) + 1);
  return Position{static_cast<int>(line), static_cast<int>(col)};
}

auto Labels(const std::vector<CompletionItem>& items) -> std::vector<std::string> {
  std::vector<std::string> labels{};
  for (const auto& item : items)
    labels.push_back(item.label);
  return labels;
}

auto Contains(const std::vector<std::string>& labels, const std::string& label) -> bool {
  return std::find(labels.begin(), labels.end(), label) != labels.end();
}

auto Complete(const std::string& marked) -> std::vector<std::string> {
  const auto [text, offset] = Mark(marked);
  auto doc = Open(text);
  return Labels(GetCompletions(*doc, offset));
}

auto Apply(const Document& doc, std::vector<TextEdit> edits) -> std::string {
  std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
    return std::tie(a.range.start.line, a.range.start.character) > std::tie(b.range.start.line, b.range.start.character);
  });
  auto text = doc.GetContents();
  for (const auto& edit : edits) {
    const auto begin = doc.ToOffset(edit.range.start);
    const auto end = doc.ToOffset(edit.range.end);
    text.replace(begin, end - begin, edit.newText);
  }
  return text;
}
}  // namespace

// ---- diagnostics ------------------------------------------------------------

TEST(DiagnosticTest, SeverityIsMappedByName) {
  EXPECT_EQ(ToLspSeverity(syntax::Severity::kError), Diagnostic::kError);
  EXPECT_EQ(ToLspSeverity(syntax::Severity::kWarning), Diagnostic::kWarning);
  EXPECT_EQ(ToLspSeverity(syntax::Severity::kStyle), Diagnostic::kHint);
  EXPECT_EQ(ToLspSeverity(syntax::Severity::kNote), Diagnostic::kHint);
}

TEST(DiagnosticTest, SerializesWithAPlainStringMessageAndKuraSource) {
  auto diagnostic = Diagnostic::Error(Range{{1, 2}, {1, 5}}, 34, "nope");
  json_t* doc = json_object();
  diagnostic >> doc;

  EXPECT_STREQ(json_string_value(json_object_get(doc, "message")), "nope");
  EXPECT_STREQ(json_string_value(json_object_get(doc, "source")), "kura");
  EXPECT_STREQ(json_string_value(json_object_get(doc, "code")), "K0034");
  EXPECT_EQ(json_integer_value(json_object_get(doc, "severity")), 1);
  json_decref(doc);
}

// ---- binder -------------------------------------------------------------------

TEST(BinderTest, ResolvesParametersVariantsAndFields) {
  auto doc = Open(kProgram);
  const auto* bindings = doc->GetBindings();
  ASSERT_NE(bindings, nullptr);

  // `state` in the body of `update` is its parameter.
  const auto use = doc->ToOffset(At(kProgram, "state", 3));
  const auto* occurrence = bindings->FindOccurrenceAt(use);
  ASSERT_NE(occurrence, nullptr);
  EXPECT_EQ(bindings->GetDecl(occurrence->decl).kind, DeclKind::kParam);

  // `Increment` in the match is the variant, not a type.
  const auto* variant = bindings->FindOccurrenceAt(doc->ToOffset(At(kProgram, "Increment", 1)));
  ASSERT_NE(variant, nullptr);
  EXPECT_EQ(bindings->GetDecl(variant->decl).kind, DeclKind::kVariant);
}

TEST(BinderTest, FieldAccessIsTiedToTheRecordWhenItsTypeIsKnown) {
  auto doc = Open(kProgram);
  const auto* bindings = doc->GetBindings();

  // `state.value`: `state` is a `State`, so `value` is State's field, for certain.
  const auto* value = bindings->FindOccurrenceAt(doc->ToOffset(At(kProgram, "value", 3)));
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(bindings->GetDecl(value->decl).kind, DeclKind::kField);
  EXPECT_FALSE(value->is_guess);

  // `size.width`: `size` is a `Size`, which is not declared here, so `width` is
  // not State's field even though State has one called that.
  const auto offset = doc->ToOffset(At(kProgram, "size.width", 0, 5));
  const auto* width = bindings->FindOccurrenceAt(offset);
  EXPECT_EQ(width, nullptr);
}

TEST(BinderTest, LetIsOnlyVisibleAfterItsStatement) {
  const std::string text = "fn f(): Int => {\n  let a = 1\n  let b = a\n  b\n}\n";
  auto doc = Open(text);
  const auto* bindings = doc->GetBindings();
  const auto* use = bindings->FindOccurrenceAt(doc->ToOffset(At(text, "a", 1, 0)));
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(bindings->GetDecl(use->decl).kind, DeclKind::kLet);
}

TEST(BinderTest, DoesNotResolveANameThatIsNotDeclared) {
  const std::string text = "fn f(): Int => missing\n";
  auto doc = Open(text);
  EXPECT_EQ(doc->GetBindings()->FindOccurrenceAt(doc->ToOffset(At(text, "missing"))), nullptr);
}

// ---- navigation ---------------------------------------------------------------

TEST(NavigationTest, DefinitionOfAVariantUse) {
  auto doc = Open(kProgram);
  const auto target = GetDefinition(*doc, At(kProgram, "Increment", 1, 2));
  ASSERT_TRUE(target);
  EXPECT_EQ(target->range.start.line, 0);
}

TEST(NavigationTest, NoDefinitionForABuiltin) {
  auto doc = Open(kProgram);
  EXPECT_FALSE(GetDefinition(*doc, At(kProgram, "Int", 0, 1)));
}

TEST(NavigationTest, ReferencesListTheDeclarationAndEveryUse) {
  auto doc = Open(kProgram);
  const auto with = GetReferences(*doc, At(kProgram, "Increment", 0, 2), true);
  const auto without = GetReferences(*doc, At(kProgram, "Increment", 0, 2), false);
  EXPECT_EQ(with.size(), 3u);
  EXPECT_EQ(without.size(), 2u);
}

TEST(NavigationTest, RenameChangesEveryUse) {
  auto doc = Open(kProgram);
  const auto edits = Rename(*doc, At(kProgram, "Increment", 0, 2), "Bump");
  ASSERT_TRUE(edits);
  const auto renamed = Apply(*doc, *edits);
  EXPECT_EQ(renamed.find("Increment"), std::string::npos);
  EXPECT_NE(renamed.find("Bump | Set(Int)"), std::string::npos);
  EXPECT_NE(renamed.find("on-click: Bump"), std::string::npos);
}

TEST(NavigationTest, RenameKeepsTheCaseThatTellsTypesFromValues) {
  auto doc = Open(kProgram);
  EXPECT_FALSE(Rename(*doc, At(kProgram, "Increment", 0, 2), "bump"));
  EXPECT_FALSE(Rename(*doc, At(kProgram, "doubled", 0, 2), "Doubled"));
  EXPECT_FALSE(Rename(*doc, At(kProgram, "doubled", 0, 2), "fn"));
  EXPECT_FALSE(Rename(*doc, At(kProgram, "doubled", 0, 2), "two words"));
}

TEST(NavigationTest, RenameRefusesANameThatIsTaken) {
  const std::string text = "fn f(a: Int, b: Int): Int => a + b\n";
  auto doc = Open(text);
  EXPECT_FALSE(Rename(*doc, At(text, "a", 1, 0), "b"));
}

TEST(NavigationTest, RenameAParameterUsedAsAPun) {
  const std::string text = "fn f(width: Int): Node => #block(width:)\n";
  auto doc = Open(text);
  const auto edits = Rename(*doc, At(text, "width", 0, 1), "w");
  ASSERT_TRUE(edits);
  // `width:` is `width: width`; only the value changes.
  EXPECT_EQ(Apply(*doc, *edits), "fn f(w: Int): Node => #block(width: w)\n");
}

TEST(NavigationTest, RenameAFieldRenamesItsUses) {
  auto doc = Open(kProgram);
  const auto edits = Rename(*doc, At(kProgram, "width", 0, 1), "w");
  ASSERT_TRUE(edits);
  const auto renamed = Apply(*doc, *edits);
  EXPECT_NE(renamed.find("w: Float"), std::string::npos);
  EXPECT_NE(renamed.find("{ state | w: size.width }"), std::string::npos);
}

TEST(NavigationTest, HighlightsMarkTheDeclarationAsAWrite) {
  auto doc = Open(kProgram);
  const auto highlights = GetHighlights(*doc, At(kProgram, "Increment", 0, 2));
  ASSERT_EQ(highlights.size(), 3u);
  EXPECT_EQ(highlights.front().kind, Highlight::kWrite);
}

// ---- hover --------------------------------------------------------------------

TEST(HoverTest, FunctionShowsItsSignature) {
  auto doc = Open(kProgram);
  const auto hover = GetHover(*doc, At(kProgram, "update", 0, 1));
  ASSERT_TRUE(hover);
  EXPECT_NE(hover->contents.value.find("fn update(msg: Message, state: State): State"), std::string::npos);
}

TEST(HoverTest, NothingToSayIsNotAnError) {
  auto doc = Open(kProgram);
  EXPECT_FALSE(GetHover(*doc, Position{0, 0}));
}

TEST(HoverTest, DoesNotCrashOnAParameterWithoutAType) {
  const std::string text = "fn f(a): Int => a\nfn g(f: (Int) => Int): Int => f(1)\n";
  auto doc = Open(text);
  for (int line = 0; line < 2; line++) {
    for (int ch = 0; ch < 30; ch++)
      GetHover(*doc, Position{line, ch});
  }
}

TEST(HoverTest, ElementPropertyShowsWhatItAccepts) {
  auto doc = Open(kProgram);
  const auto hover = GetHover(*doc, At(kProgram, "direction", 0, 2));
  ASSERT_TRUE(hover);
  EXPECT_NE(hover->contents.value.find("direction"), std::string::npos);
  EXPECT_NE(hover->contents.value.find("\"column\""), std::string::npos);
}

TEST(HoverTest, LeadingCommentIsPartOfTheHover) {
  const std::string text = "// Adds one.\n// Twice, in fact.\nfn inc(a: Int): Int => a + 1\n";
  auto doc = Open(text);
  const auto hover = GetHover(*doc, At(text, "inc", 0, 1));
  ASSERT_TRUE(hover);
  EXPECT_NE(hover->contents.value.find("Adds one.\nTwice, in fact."), std::string::npos);
}

// ---- outline ------------------------------------------------------------------

TEST(OutlineTest, TypesHaveTheirFieldsAndVariantsAsChildren) {
  auto doc = Open(kProgram);
  const auto symbols = GetOutline(*doc);
  ASSERT_GE(symbols.size(), 4u);
  EXPECT_EQ(symbols[0].name, "Message");
  EXPECT_EQ(symbols[0].children.size(), 3u);
  EXPECT_EQ(symbols[1].name, "State");
  EXPECT_EQ(symbols[1].children.size(), 2u);
}

TEST(OutlineTest, FoldsBodiesButKeepsTheClosingBracketVisible) {
  auto doc = Open(kProgram);
  const auto folds = GetFoldingRanges(*doc);
  const auto type = std::find_if(folds.begin(), folds.end(), [](const FoldingRange& f) { return f.start_line == 2; });
  ASSERT_NE(type, folds.end());
  EXPECT_EQ(type->end_line, 4);
}

// ---- completion ---------------------------------------------------------------

TEST(CompletionTest, FieldsAfterADotComeFromTheTypeOfWhatIsInFront) {
  const auto labels = Complete("type S = { a: Int, b: Int }\ntype T = { c: Int }\nfn f(s: S): Int => s.§\n");
  EXPECT_TRUE(Contains(labels, "a"));
  EXPECT_TRUE(Contains(labels, "b"));
  EXPECT_FALSE(Contains(labels, "c"));
}

TEST(CompletionTest, FieldsOfABuiltinRecord) {
  const auto labels = Complete("fn f(s: Size): Float => s.§\n");
  EXPECT_TRUE(Contains(labels, "width"));
  EXPECT_TRUE(Contains(labels, "height"));
}

TEST(CompletionTest, FieldsInsideAnInterpolatedString) {
  const auto labels = Complete("type S = { a: Int }\nfn f(s: S): Node => #text(\"{s.§}\")\n");
  EXPECT_TRUE(Contains(labels, "a"));
}

TEST(CompletionTest, ElementsAfterAHash) {
  const auto labels = Complete("fn f(): Node => #§\n");
  EXPECT_TRUE(Contains(labels, "button"));
  EXPECT_TRUE(Contains(labels, "block"));
}

TEST(CompletionTest, PropertiesOfTheElementBeingWritten) {
  const auto labels = Complete("fn f(): Node => #block(§)\n");
  EXPECT_TRUE(Contains(labels, "direction"));
  EXPECT_TRUE(Contains(labels, "width"));
}

TEST(CompletionTest, AProperyAlreadyWrittenIsNotOfferedAgain) {
  const auto labels = Complete("fn f(): Node => #block(width: 1px, §)\n");
  EXPECT_FALSE(Contains(labels, "width"));
  EXPECT_TRUE(Contains(labels, "height"));
}

TEST(CompletionTest, ValuesOfAnEnumeratedProperty) {
  const auto labels = Complete("fn f(): Node => #block(direction: §)\n");
  EXPECT_TRUE(Contains(labels, "\"row\""));
  EXPECT_TRUE(Contains(labels, "\"column\""));
}

TEST(CompletionTest, AnEventPropertyOffersTheMessages) {
  const auto labels = Complete("type M = Go | Stop(Int)\nfn f(): Node => #button(on-click: §)\n");
  EXPECT_TRUE(Contains(labels, "Go"));
  EXPECT_TRUE(Contains(labels, "Stop"));
}

TEST(CompletionTest, TypesAfterAColonInASignature) {
  const auto labels = Complete("type Mine = A | B\nfn f(x: §): Int => 1\n");
  EXPECT_TRUE(Contains(labels, "Mine"));
  EXPECT_TRUE(Contains(labels, "Int"));
  EXPECT_FALSE(Contains(labels, "if"));
}

TEST(CompletionTest, NamesInScopeInAnExpression) {
  const auto labels = Complete("fn helper(): Int => 1\nfn f(param: Int): Int => {\n  let local = 2\n  §\n}\n");
  EXPECT_TRUE(Contains(labels, "param"));
  EXPECT_TRUE(Contains(labels, "local"));
  EXPECT_TRUE(Contains(labels, "helper"));
}

TEST(CompletionTest, ALetIsNotOfferedBeforeItsStatement) {
  const auto labels = Complete("fn f(): Int => {\n  let a = §\n  let later = 2\n  a\n}\n");
  EXPECT_FALSE(Contains(labels, "later"));
  EXPECT_FALSE(Contains(labels, "a"));
}

TEST(CompletionTest, NothingInAComment) {
  EXPECT_TRUE(Complete("fn f(): Int => 1 // see §\n").empty());
}

TEST(CompletionTest, DirectivesAfterAnAt) {
  const auto labels = Complete("@§\nfn f(): Int => 1\n");
  EXPECT_TRUE(Contains(labels, "pure"));
}

TEST(CompletionContextTest, CallSiteCountsArguments) {
  const std::string text = "fn f(): Int => add(1, g(2, 3), )";
  const auto site = FindEnclosingCall(text, static_cast<uint32_t>(text.size() - 1));
  ASSERT_TRUE(site);
  EXPECT_EQ(site->callee, std::vector<std::string>{"add"});
  EXPECT_EQ(site->active_index, 2);
}

// ---- signature help -----------------------------------------------------------

TEST(SignatureHelpTest, MarksTheArgumentBeingWritten) {
  const std::string text = "fn add(a: Int, b: Int): Int => a + b\nfn main(): Int => add(1, 2)\n";
  auto doc = Open(text);
  const auto help = GetSignatureHelp(*doc, At(text, "add(1, ", 0, 7));
  ASSERT_TRUE(help);
  EXPECT_EQ(help->label, "add(a: Int, b: Int): Int");
  EXPECT_EQ(help->active_parameter, 1);
}

TEST(SignatureHelpTest, NothingOutsideACall) {
  const std::string text = "fn add(a: Int): Int => a\n";
  auto doc = Open(text);
  EXPECT_FALSE(GetSignatureHelp(*doc, Position{0, 3}));
}

// ---- formatting ---------------------------------------------------------------

TEST(FormattingTest, FormatsAndIsIdempotent) {
  const std::string text = "fn   add(a:Int,b:Int):Int=>a+b\n";
  auto doc = Open(text);
  const auto edits = FormatDocument(*doc, FormattingOptions{});
  ASSERT_TRUE(edits);
  ASSERT_EQ(edits->size(), 1u);
  const auto formatted = Apply(*doc, *edits);
  EXPECT_EQ(formatted, "fn add(a: Int, b: Int): Int => a + b\n");

  auto again = Open(formatted);
  const auto second = FormatDocument(*again, FormattingOptions{});
  ASSERT_TRUE(second);
  EXPECT_TRUE(second->empty());
}

TEST(FormattingTest, LeavesAFileWithSyntaxErrorsAlone) {
  auto doc = Open("fn broken( =>\n");
  EXPECT_FALSE(FormatDocument(*doc, FormattingOptions{}));
}

// ---- uri ----------------------------------------------------------------------

TEST(UriTest, RoundTripsPathsWithSpaces) {
  const auto uri = PathToUri("/home/me/my project/a.kui");
  EXPECT_EQ(uri, "file:///home/me/my%20project/a.kui");
  EXPECT_EQ(UriToPath(uri), "/home/me/my project/a.kui");
  EXPECT_FALSE(UriToPath("untitled:Untitled-1"));
}
}  // namespace kura::lsp
