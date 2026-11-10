#ifndef KURA_LINTER_RULE_H
#define KURA_LINTER_RULE_H

#include "frontend/syntax/diagnostic.h"
#include "frontend/syntax/parser.h"

namespace kura::lint {
#define FOR_EACH_LINTER_RULE_CODE(V) \
  V(FnNameShouldBeKebab, 1)          \
  V(RecordNameShouldBePascal, 2)     \
  V(FnShouldHaveReturnType, 3)

// clang-format off
enum LinterRuleCodes : uint64_t {
#define DEFINE_CODE(Name, Value) \
  k##Name##Code = syntax::kTotalNumberOfParserDiagnosticCodes + (Value),

  FOR_EACH_LINTER_RULE_CODE(DEFINE_CODE)
#undef DEFINE_CODE
};
// clang-format on

struct Rule {
  virtual void Apply(const syntax::Node& root, syntax::DiagnosticList& diagnostics) = 0;
};

#define DECLARE_SIMPLE_RULE(Name)                                                       \
  struct Name##Rule : public Rule {                                                     \
    void Apply(const syntax::Node& root, syntax::DiagnosticList& diagnostics) override; \
  };

#define RULE_FN(Name) void Name##Rule::Apply(const syntax::Node& root, syntax::DiagnosticList& diagnostics)
}  // namespace kura::lint

#endif  // KURA_LINTER_RULE_H
