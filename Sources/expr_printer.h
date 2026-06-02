#ifndef KURA_EXPR_PRINTER_H
#define KURA_EXPR_PRINTER_H

#include "expr.h"

namespace kura::expr {
// TODO(@s0cks): implement
class ExprPrinter : public ExprVisitor {
  static constexpr const auto kDefaultIndent = 0;

 private:
  Indent indent_{};

 public:
  explicit ExprPrinter(const uint64_t indent = kDefaultIndent) :
    ExprVisitor(),
    indent_(indent) {}
  ~ExprPrinter() override = default;

  auto GetIndent() const -> const Indent& {
    return indent_;
  }

  // clang-format off
#define DEFINE_VISIT(Name) \
  virtual auto Visit##Name(Name##Expr*) -> VisitResult override;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on
 public:
  static inline auto Print(Expr* expr, const uint64_t indent = kDefaultIndent) -> VisitResult {
    ExprPrinter printer(indent);
    return expr->Accept(&printer);
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_PRINTER_H
