#include "expr_printer.h"

#include <print>

namespace kura::expr {
auto ExprPrinterBase::VisitCall(CallExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitLiteral(LiteralExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitUnary(UnaryExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitBinary(BinaryExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitList(ListExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitListComprehension(ListComprehensionExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitSpread(SpreadExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitPipeline(PipelineExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitMatch(MatchExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitSeq(SeqExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitIf(IfExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitNode(NodeExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitRecord(RecordExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitStoreProperty(StorePropertyExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitWildcardPattern(WildcardPatternExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitLiteralPattern(LiteralPatternExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitLoadLocal(LoadLocalExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitStoreLocal(StoreLocalExpr* expr) -> VisitResult {
  return true;
}

auto ExprPrinterBase::VisitGetProperty(GetPropertyExpr* expr) -> VisitResult {
  return true;
}
}  // namespace kura::expr
