#ifndef KURA_FORMATTER_H
#define KURA_FORMATTER_H

#include "config.h"
#include "frontend/syntax/parser.h"

namespace kura::fmt {

class Formatter {
 private:
  std::ostream& stream_;
  Config config_;

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

 public:
  explicit Formatter(std::ostream& stream, Config config) :
    stream_(stream),
    config_(std::move(config)) {}
  ~Formatter() = default;

  auto Format(syntax::SyntaxTree* tree) -> bool;
};

}  // namespace kura::fmt

#endif  // KURA_FORMATTER_H
