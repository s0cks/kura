#ifndef KURA_EXPR_BUILDER_H
#define KURA_EXPR_BUILDER_H

#include "KuraBaseVisitor.h"
#include "KuraParser.h"
#include "expr.h"

namespace kura::expr {
class ExprBuilder : public KuraBaseVisitor {
 public:
  ExprBuilder() = default;
  ~ExprBuilder() override = default;

  auto visitFuncDef(KuraParser::FuncDefContext* ctx) -> std::any override;
  auto visitLiteral(KuraParser::LiteralContext* ctx) -> std::any override;
  auto visitExpression(KuraParser::ExpressionContext* ctx) -> std::any override;
};
}  // namespace kura::expr

#endif  // KURA_EXPR_BUILDER_H
