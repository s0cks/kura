#include "dom/printer.h"

#include "frontend/expr_printer.h"
#include "object.h"

namespace kura::dom {
auto Printer::VisitNode(Node* node) -> bool {
  stream_ << indent_ << node->GetName();

  if (const auto expr = node->GetExpr())
    stream_ << (node->IsDynamic() ? " ~" : " ") << expr::ExprPrinter::ToString(expr);

  for (const auto& property : node->GetProperties()) {
    stream_ << ' ' << property.def->name << ": ";
    if (property.IsConstant()) {
      stream_ << property.constant->ToString();
    } else {
      stream_ << '~' << expr::ExprPrinter::ToString(property.expr);
    }
  }

  if (!node->GetTags().empty()) {
    stream_ << " [tags: ";
    for (size_t idx = 0; idx < node->GetTags().size(); idx++)
      stream_ << (idx > 0 ? ", " : "") << node->GetTags()[idx];

    stream_ << ']';
  }

  stream_ << std::endl;

  ++indent_;
  const auto result = VisitChildren(node);
  --indent_;
  return result;
}
}  // namespace kura::dom
