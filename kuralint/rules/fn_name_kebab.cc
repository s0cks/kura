#include "rules/fn_name_kebab.h"

#include <cctype>
#include <string>
#include <string_view>

#include "rule.h"

namespace kura::lint::rules {
bool is_kebab_case(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  if (str.front() == '-' || str.back() == '-') {
    return false;
  }

  bool last_was_hyphen = false;

  for (char ch : str) {
    if (ch == '-') {
      if (last_was_hyphen) {
        return false;
      }
      last_was_hyphen = true;
    } else if (std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch))) {
      last_was_hyphen = false;
    } else {
      return false;
    }
  }

  return true;
}

static inline auto camelToKebab(std::string_view str) -> std::string {
  if (str.empty())
    return "";

  std::string result;
  result.reserve(str.size() + 4);

  result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(str[0]))));

  for (size_t i = 1; i < str.size(); ++i) {
    char current = str[i];
    if (std::isupper(static_cast<unsigned char>(current))) {
      char prev = str[i - 1];
      if (std::islower(static_cast<unsigned char>(prev)) || std::isdigit(static_cast<unsigned char>(prev))) {
        result.push_back('-');
      }
      result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(current))));
    } else {
      result.push_back(current);
    }
  }

  return result;
}

RULE_FN(FnNameKebabCase) {
  if (const auto* fn = root.As<syntax::FnDecl>()) {
    if (!is_kebab_case(fn->name.text)) {
      diagnostics.Add(syntax::Diagnostic{
          .severity = syntax::Severity::kStyle,
          .code = kFnNameShouldBeKebabCode,
          .message = "Function names should be kebab-case",
          .span = fn->name.span,
          .span_label = std::format("did you mean '{}'", camelToKebab(fn->name.text)),
      });
    }

    return;
  }

  root.ForEachChild([this, &diagnostics](const syntax::Node* child) {
    Apply(*child, diagnostics);
  });
}
}  // namespace kura::lint::rules
