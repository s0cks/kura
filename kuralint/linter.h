#ifndef KURA_LINTER_H
#define KURA_LINTER_H

#include "rules/rule.h"

namespace kura::lint {
class Linter {
 private:
  std::vector<std::shared_ptr<Rule>> rules_{};

 public:
  Linter();
  ~Linter() = default;

  void Apply(const syntax::SyntaxTree& tree, syntax::DiagnosticList& diagnostics);
};
}  // namespace kura::lint

#endif  // KURA_LINTER_H
