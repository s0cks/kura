#include "linter.h"

#include "rules/all.h"

namespace kura::lint {
Linter::Linter() {
  rules_.push_back(std::make_shared<rules::FnNameKebabCaseRule>());
  rules_.push_back(std::make_shared<rules::RecordNamePascalCaseRule>());
  rules_.push_back(std::make_shared<rules::FnMissingReturnTypeRule>());
}

void Linter::Apply(const syntax::SyntaxTree& tree, syntax::DiagnosticList& diagnostics) {
  for (const auto& rule : rules_)
    rule->Apply(*tree.GetRoot(), diagnostics);
}
}  // namespace kura::lint
