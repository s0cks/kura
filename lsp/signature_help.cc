#include "lsp/signature_help.h"

#include <algorithm>
#include <format>

#include "dom/kind.h"
#include "dom/property.h"
#include "lsp/completion.h"
#include "lsp/describe.h"

namespace kura::lsp {
namespace {
auto ForFunction(const Document& doc, const syntax::FnDecl& fn, const int index) -> SignatureHelp {
  SignatureHelp help{};
  help.label = SignatureOf(fn).substr(3);  // without `fn `
  help.documentation = LeadingComment(doc.GetContents(), fn.span.begin);
  for (const auto* p : fn.params) {
    if (const auto* param = p->As<syntax::Param>())
      help.parameters.push_back(std::format("{}: {}", param->name.text, TypeToString(param->type)));
  }
  help.active_parameter = index < static_cast<int>(help.parameters.size()) ? index : -1;
  return help;
}
}  // namespace

auto GetSignatureHelp(Document& doc, const Position& pos, ModuleResolver* modules) -> std::optional<SignatureHelp> {
  const auto offset = doc.ToOffset(pos);
  const auto site = FindEnclosingCall(doc.GetContents(), offset);
  if (!site)
    return std::nullopt;

  if (site->is_element) {
    const auto kind = dom::FindKind(site->tag);
    if (!kind)
      return std::nullopt;

    SignatureHelp help{};
    help.label = "#" + site->tag + "(";
    int index = 0;
    for (const auto* def : dom::PropertiesOf(*kind)) {
      const std::string name(def->name);
      const auto part = std::format("{}: {}", name, dom::DescribeAccepted(*def));
      if (index > 0)
        help.label += ", ";
      help.label += part;
      help.parameters.push_back(part);
      if (name == site->active_name)
        help.active_parameter = index;
      index++;
    }
    help.label += ")";
    return help;
  }

  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return std::nullopt;

  // `hp.percent(`: a function of another file.
  if (site->callee.size() == 2 && modules) {
    if (const auto* alias = bindings->Lookup(site->callee[0], Ns::kValue, offset); alias && alias->kind == DeclKind::kAlias) {
      const auto* use = alias->node->As<syntax::UseDecl>();
      auto* module = use ? modules->LoadUse(doc, *use) : nullptr;
      if (module) {
        for (const auto* decl : GetTopLevelDecls(*module, Ns::kValue)) {
          if (decl->name == site->callee[1] && decl->kind == DeclKind::kFn)
            return ForFunction(*module, *decl->node->As<syntax::FnDecl>(), site->active_index);
        }
      }
    }
    return std::nullopt;
  }

  if (site->callee.size() != 1)
    return std::nullopt;

  const auto& name = site->callee.front();
  if (const auto* decl = bindings->Lookup(name, Ns::kValue, offset)) {
    if (const auto* fn = decl->node ? decl->node->As<syntax::FnDecl>() : nullptr)
      return ForFunction(doc, *fn, site->active_index);
    if (const auto* ext = decl->node ? decl->node->As<syntax::ExternDecl>() : nullptr) {
      SignatureHelp help{};
      help.label = SignatureOf(*ext).substr(SignatureOf(*ext).find(" fn ") + 4);
      for (const auto* p : ext->params) {
        if (const auto* param = p->As<syntax::Param>())
          help.parameters.push_back(std::format("{}: {}", param->name.text, TypeToString(param->type)));
      }
      help.active_parameter = site->active_index < static_cast<int>(help.parameters.size()) ? site->active_index : -1;
      return help;
    }
  }

  if (const auto* decl = bindings->Lookup(name, Ns::kType, offset); decl && decl->kind == DeclKind::kVariant) {
    const auto* v = decl->node ? decl->node->As<syntax::VariantType>() : nullptr;
    if (!v || !v->payload)
      return std::nullopt;
    SignatureHelp help{};
    help.label = TypeToString(v);
    help.parameters.push_back(TypeToString(v->payload));
    help.active_parameter = 0;
    return help;
  }
  return std::nullopt;
}
}  // namespace kura::lsp
