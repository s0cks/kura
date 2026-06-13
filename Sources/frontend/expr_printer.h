#ifndef KURA_EXPR_PRINTER_H
#define KURA_EXPR_PRINTER_H

#include "frontend/expr.h"
#include "printer.h"

namespace kura::expr {
// TODO(@s0cks): implement
class ExprPrinter : Printer, public ExprVisitor {
  static constexpr const auto kDefaultIndent = 0;

 public:
  explicit ExprPrinter(std::ostream& stream, const Indent indent = {}) :
    Printer(stream, std::move(indent)),
    ExprVisitor() {}
  ~ExprPrinter() override = default;

  // clang-format off
#define DEFINE_VISIT(Name) \
   auto Visit##Name(Name##Expr*) -> VisitResult override;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on

 public:
  static inline auto Print(std::ostream& stream, Expr* expr, const Indent indent = {}) -> VisitResult {
    ExprPrinter printer(stream, std::move(indent));
    return expr->Accept(&printer);
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_PRINTER_H
