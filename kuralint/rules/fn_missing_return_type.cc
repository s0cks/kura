#include "rules/fn_missing_return_type.h"

namespace kura::lint::rules {
RULE_FN(FnMissingReturnType) {
  if (const auto* fn = root.As<syntax::FnDecl>()) {
    if (fn->name.text != "set" && !fn->result) {
      uint64_t begin = fn->body->span.begin - 4;
      if (!fn->set_rules.empty())
        begin = fn->set_rules[0]->span.begin - 6;

      diagnostics.Add(syntax::Diagnostic{
          .severity = syntax::Severity::kStyle,
          .code = kFnShouldHaveReturnTypeCode,
          .message = "Functions should have a return type",
          .span = syntax::Span(begin, begin + 1),
          .span_label = "Missing type annotation here (e.g., fn my-func(): String)",
      });
    }

    return;
  }

  root.ForEachChild([this, &diagnostics](const syntax::Node* child) {
    Apply(*child, diagnostics);
  });
}
}  // namespace kura::lint::rules
