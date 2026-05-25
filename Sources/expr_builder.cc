#include "expr_builder.h"

namespace kura::expr {
auto ExprBuilder::visitFuncDef(KuraParser::FuncDefContext* ctx) -> std::any {
  throw std::runtime_error("invalid FunctionDef");
}

auto ExprBuilder::visitExpression(KuraParser::ExpressionContext* ctx) -> std::any {
  if (ctx->expression().size() == 2) {
    const auto lhs = std::any_cast<expr::Expr*>(visit(ctx->expression(0)));
    const auto rhs = std::any_cast<expr::Expr*>(visit(ctx->expression(1)));
    const auto& op = ctx->op->getText();
    if (op == "+")
      return BinaryExpr::NewAdd(lhs, rhs);
    else if (op == "-")
      return BinaryExpr::NewSubtract(lhs, rhs);
    else if (op == "*")
      return BinaryExpr::NewMultiply(lhs, rhs);
    else if (op == "/")
      return BinaryExpr::NewDivide(lhs, rhs);
    throw std::runtime_error("invalid BinaryExpr");
  }

  if (ctx->literal())
    return visit(ctx->literal());
  throw std::runtime_error("invalid Expr");
}

auto ExprBuilder::visitLiteral(KuraParser::LiteralContext* ctx) -> std::any {
  throw std::runtime_error("invalid LiteralExpr");
}
}  // namespace kura::expr
