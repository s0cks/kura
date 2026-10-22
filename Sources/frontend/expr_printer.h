#ifndef KURA_EXPR_PRINTER_H
#define KURA_EXPR_PRINTER_H

#include <sstream>

#include "frontend/expr.h"
#include "printer.h"

namespace kura::expr {
// Prints an Expr tree as a single-line s-expression, e.g.
//   (seq (set x 1) (binary + x 2))
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

 private:
  void PrintChild(Expr* expr);
  void PrintChildren(const ExprList& exprs);
  void PrintDirectives(const DirectiveList& directives);

 public:
  static inline auto ToString(Expr* expr) -> std::string {
    std::stringstream ss{};
    Print(ss, expr);
    return ss.str();
  }

  static inline auto Print(std::ostream& stream, Expr* expr, const Indent indent = {}) -> VisitResult {
    ExprPrinter printer(stream, std::move(indent));
    return expr->Accept(&printer);
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_PRINTER_H
