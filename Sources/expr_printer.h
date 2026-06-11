#ifndef KURA_EXPR_PRINTER_H
#define KURA_EXPR_PRINTER_H

#include "expr.h"
#include "printer.h"

namespace kura::expr {
// TODO(@s0cks): implement

class ExprPrinterBase : public ExprVisitor {
 private:
  ExprPrinterBase() = default;

 public:
  ~ExprPrinterBase() override = default;

  // clang-format off
#define DEFINE_VISIT(Name) \
   auto Visit##Name(Name##Expr*) -> VisitResult override;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on
};

template <typename Stream = std::ostream>
class ExprPrinter : public Printer<Stream> {
  static constexpr const auto kDefaultIndent = 0;

 public:
  explicit ExprPrinter(Stream& stream, const Indent indent = {}) :
    ExprVisitor(stream, std::move(indent)) {}
  ~ExprPrinter() override = default;

 public:
  static inline auto Print(Expr* expr, const uint64_t indent = kDefaultIndent) -> VisitResult {
    ExprPrinter printer(indent);
    return expr->Accept(&printer);
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_PRINTER_H
