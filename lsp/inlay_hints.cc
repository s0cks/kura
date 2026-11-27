#include "lsp/inlay_hints.h"

#include "lsp/describe.h"

namespace kura::lsp {
namespace {
auto IsLiteral(const syntax::Node* node) -> bool {
  if (!node)
    return false;
  switch (node->GetKind()) {
    case syntax::Node::kNumberLit:
    case syntax::Node::kUnitLit:
    case syntax::Node::kStringLit:
    case syntax::Node::kBoolLit:
    case syntax::Node::kNoneLit:
      return true;
    default:
      return false;
  }
}

void CollectCalls(Document& doc, const Bindings& bindings, const syntax::Node* node, const Range& range,
                  std::vector<InlayHint>& hints) {
  if (!node)
    return;

  if (const auto* call = node->As<syntax::Call>()) {
    const auto* callee = call->callee ? call->callee->As<syntax::VarRef>() : nullptr;
    const auto* occurrence = callee ? bindings.FindOccurrenceAt(callee->name.span.begin) : nullptr;
    const auto* fn = occurrence && bindings.GetDecl(occurrence->decl).node
                         ? bindings.GetDecl(occurrence->decl).node->As<syntax::FnDecl>()
                         : nullptr;
    if (fn) {
      for (size_t i = 0; i < call->args.size() && i < fn->params.size(); i++) {
        const auto* param = fn->params[i]->As<syntax::Param>();
        // Only a literal says nothing about what it is for; a name usually does.
        if (!param || !IsLiteral(call->args[i]))
          continue;
        const auto position = doc.ToPosition(call->args[i]->span.begin);
        if (position.line < range.start.line || position.line > range.end.line)
          continue;
        hints.push_back(InlayHint{.position = position,
                                  .label = param->name.text + ":",
                                  .kind = InlayHint::kParameter,
                                  .padding_right = true});
      }
    }
  }

  node->ForEachChild([&](const syntax::Node* child) { CollectCalls(doc, bindings, child, range, hints); });
}
}  // namespace

auto GetInlayHints(Document& doc, const Range& range) -> std::vector<InlayHint> {
  std::vector<InlayHint> hints{};
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return hints;

  for (const auto& decl : bindings->GetDecls()) {
    const bool inferable = decl.kind == DeclKind::kLet || decl.kind == DeclKind::kPatternBinding ||
                           decl.kind == DeclKind::kForElement;
    if (!inferable)
      continue;

    const auto* type = bindings->GetDeclType(decl.id);
    if (!type)
      continue;

    // A `let` written with its type already says it.
    if (const auto* b = decl.node ? decl.node->As<syntax::Binding>() : nullptr; b && b->annotation)
      continue;

    const auto position = doc.ToPosition(decl.name_span.end);
    if (position.line < range.start.line || position.line > range.end.line)
      continue;
    hints.push_back(InlayHint{.position = position, .label = ": " + TypeToString(type), .kind = InlayHint::kType});
  }

  CollectCalls(doc, *bindings, doc.GetRoot(), range, hints);
  return hints;
}
}  // namespace kura::lsp
