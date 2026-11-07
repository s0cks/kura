#include "lsp/hover.h"

#include "frontend/syntax/ast.h"

namespace kura::lsp {
auto ToHoverInfo(std::stringstream& ss, const syntax::Node* node) -> bool {
  if (const auto* fn = node->As<syntax::FnDecl>()) {
    ss << "# " << fn->name.text << "\n";
    ss << "---" << "\n";

    if (!fn->params.empty()) {
      ss << "\n";
      ss << "params: " << "\n";
      for (const auto& param : fn->params) {
        const auto& p = param->As<syntax::Param>();
        const auto& name = p->name.text;
        const auto& type = p->type->As<syntax::NamedType>()->name.text;
        ss << " - " << type << " " << name << "\n";
      }
    }

    ss << "\n\n";
    return true;
  } else if (const auto* param = node->As<syntax::Param>()) {
    ss << "# " << param->name.text << "\n";
    ss << "---" << "\n";
    ss << "Type: " << param->type->As<syntax::NamedType>()->name.text << "\n";
    ss << "\n\n";
    return true;
  } else if (const auto* binding = node->As<syntax::Binding>()) {
    ss << "# " << binding->name.text << "\n";
    ss << "---" << "\n";
    ss << "\n\n";
    return true;
  } else if (const auto* e = node->As<syntax::UiElement>()) {
    ss << "# " << e->tag.text << "\n";
    ss << "---" << "\n";

    ss << "Properties:\n";
    for (const auto& prop : e->props) {
      const auto& a = prop->As<syntax::Arg>();
      ss << " - " << a->name.text << " " << a->value->GetKindName() << "\n";
    }

    ss << "\n\n";
    return true;
  }

  return false;
}

}  // namespace kura::lsp
