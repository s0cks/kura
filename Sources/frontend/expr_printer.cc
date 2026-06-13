#include "frontend/expr_printer.h"

#include <print>

namespace kura::expr {
auto ExprPrinter::VisitCall(CallExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitLiteral(LiteralExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitUnary(UnaryExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitBinary(BinaryExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitList(ListExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitListComprehension(ListComprehensionExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitSpread(SpreadExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitPipeline(PipelineExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitMatch(MatchExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitSeq(SeqExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitIf(IfExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitNode(NodeExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitRecord(RecordExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitStoreProperty(StorePropertyExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitWildcardPattern(WildcardPatternExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitLiteralPattern(LiteralPatternExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitLoadLocal(LoadLocalExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitStoreLocal(StoreLocalExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitGetProperty(GetPropertyExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinter::VisitCase(CaseExpr* expr) -> VisitResult {
  return true;
}
}  // namespace kura::expr
