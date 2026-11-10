#include "rules/record_name_pascal.h"

#include <cctype>
#include <iostream>
#include <string>

#include "frontend/syntax/ast.h"

namespace kura::lint::rules {
static inline auto ToPascalCase(const std::string& rhs) -> std::string {
  std::string result;
  result.reserve(rhs.length());  // Prevent mid-loop reallocations

  bool capitalizeNext = true;

  for (size_t i = 0; i < rhs.length(); ++i) {
    unsigned char ch = rhs[i];

    if (ch == ' ' || ch == '_' || ch == '-') {
      capitalizeNext = true;
      continue;
    }

    if (i > 0 && std::islower(static_cast<unsigned char>(rhs[i - 1])) && std::isupper(ch)) {
      capitalizeNext = true;
    }

    if (std::isalnum(ch)) {
      if (capitalizeNext) {
        result.push_back(static_cast<char>(std::toupper(ch)));
        capitalizeNext = false;
      } else {
        result.push_back(static_cast<char>(std::tolower(ch)));
      }
    }
  }

  return result;
}

static inline auto isPascalCase(const std::string& str) -> bool {
  if (str.empty())
    return false;

  if (!std::isupper(static_cast<unsigned char>(str[0])))
    return false;

  for (size_t i = 1; i < str.length(); ++i) {
    unsigned char ch = str[i];

    if (!std::isalnum(ch))
      return false;

    if (std::isupper(ch) && std::isupper(static_cast<unsigned char>(str[i - 1]))) {
      return false;
    }
  }

  return true;
}

RULE_FN(RecordNamePascalCase) {
  if (const auto* type = root.As<syntax::TypeDecl>()) {
    if (!isPascalCase(type->name.text)) {
      diagnostics.Add(syntax::Diagnostic{
          .severity = syntax::Severity::kStyle,
          .code = kRecordNameShouldBePascalCode,
          .message = "Type names should be PascalCase",
          .span = type->name.span,
          .span_label = std::format("did you mean '{}'", ToPascalCase(type->name.text)),
      });
    }
  }

  root.ForEachChild([this, &diagnostics](const syntax::Node* rhs) {
    return Apply(*rhs, diagnostics);
  });
}
}  // namespace kura::lint::rules
