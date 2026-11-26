#ifndef KURA_FORMATTER_H
#define KURA_FORMATTER_H

#include "compiler/frontend/syntax/parser.h"
#include "formatter/config.h"
#include "formatter/document.h"

namespace kura::fmt {
class Formatter {
 private:
  Config config_;
  std::ostream& stream_;

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto GetMaxWidth() const -> int {
    return config_.max_width;
  }

  inline auto GetIndentSize() const -> int {
    return config_.indent_size;
  }

  inline auto ShouldInsertFinalNewline() const -> bool {
    return config_.insert_final_newline;
  }

  inline auto IsTypesFirst() const -> bool {
    return config_.types_first;
  }

 public:
  explicit Formatter(std::ostream& stream, Config config) :
    stream_(stream),
    config_(std::move(config)) {}
  ~Formatter() = default;

  void Render(NodePtr node) const;

  inline auto operator<<(NodePtr rhs) -> Formatter& {
    Render(rhs);
    return *this;
  }

 public:
  static void Format(std::ostream& stream, const syntax::SyntaxTree& rhs, const Config config = {});
};
}  // namespace kura::fmt

#endif  // KURA_FORMATTER_H
