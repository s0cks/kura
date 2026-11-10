#include "lsp/code_action.h"

#include "common.h"
#include "frontend/syntax/ast.h"
#include "frontend/syntax/diagnostic.h"

namespace kura::lsp {
void CodeAction::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set_new(doc, "title", json_string(title.c_str()));
  json_object_set_new(doc, "kind", json_string(kind.c_str()));
  if (isPreferred)
    json_object_set_new(doc, "isPreferred", json_true());

  json_t* edits_node = json_array();
  KASSERT(edits_node);
  for (const auto& edit : edits) {
    json_t* edit_node = json_object();
    KASSERT(edit_node);
    edit >> edit_node;
    json_array_append_new(edits_node, edit_node);
  }

  // WorkspaceEdit: { changes: { [uri]: TextEdit[] } }
  json_t* changes_node = json_object();
  KASSERT(changes_node);
  json_object_set_new(changes_node, uri.c_str(), edits_node);

  json_t* edit_node = json_object();
  KASSERT(edit_node);
  json_object_set_new(edit_node, "changes", changes_node);
  json_object_set_new(doc, "edit", edit_node);
}

namespace {
auto Overlaps(const syntax::Span& span, const syntax::Span& selection) -> bool {
  return span.Touches(selection.begin) || span.Touches(selection.end) || selection.Touches(span.begin);
}

// `fn name => ...` -> `fn name() => ...`
void AddEmptyParameterList(const Document& doc, const syntax::Diagnostic& diag, const syntax::Span& selection,
                           std::vector<CodeAction>& actions) {
  const auto* root = doc.GetRoot();
  if (!root)
    return;

  // the declaration the parser was in the middle of when it reported this
  const syntax::FnDecl* fn = nullptr;
  root->ForEachChild([&](const syntax::Node* item) {
    const auto* decl = item->As<syntax::FnDecl>();
    if (decl && decl->span.Touches(diag.span.begin))
      fn = decl;
  });
  if (!fn || !fn->params.empty())
    return;

  // from `fn` (or its directives) to the token that should have been `(`
  if (!Overlaps(syntax::Span(fn->span.begin, diag.span.end), selection))
    return;

  // `fn name` gets the parentheses right after the name; with no name at all
  // they go where the parser expected the `(`.
  const auto at = fn->name.missing ? diag.span.begin : fn->name.span.end;
  actions.push_back(CodeAction{
      .title = "Add an empty parameter list",
      .isPreferred = true,
      .uri = doc.GetUri(),
      .edits = {TextEdit{
          .range = doc.ToRange(syntax::Span(at, at)),
          .newText = "()",
      }},
  });
}
}  // namespace

auto GetCodeActions(const Document& doc, const Range& range) -> std::vector<CodeAction> {
  std::vector<CodeAction> actions{};
  if (!doc.IsTreeCurrent())
    return actions;

  const auto selection = syntax::Span(doc.ToOffset(range.start), doc.ToOffset(range.end));
  for (const auto& diag : doc.GetTree()->GetDiagnostics()) {
    if (diag.code == static_cast<uint64_t>(syntax::DiagnosticCode::kFnWithoutParens))
      AddEmptyParameterList(doc, diag, selection, actions);
  }
  return actions;
}
}  // namespace kura::lsp
