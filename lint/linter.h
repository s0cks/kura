#ifndef KURA_LINTER_H
#define KURA_LINTER_H

#include "cli/format.h"
#include "compiler/frontend/syntax/parser.h"

namespace kura::lint {
class Linter {
 private:
  cli::Format format_;

 public:
  explicit Linter(cli::Format format) :
    format_(format) {}
  ~Linter() = default;

  auto GetFormat() const -> cli::Format {
    return format_;
  }

  void Lint(const kura::syntax::SyntaxTree& rhs) noexcept;
};
}  // namespace kura::lint

#endif  // KURA_LINTER_H
