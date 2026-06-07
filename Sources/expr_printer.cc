#include "expr_printer.h"

#include <print>

namespace kura::expr {
auto ExprPrinter::VisitCall(CallExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitLiteral(LiteralExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitUnary(UnaryExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitBinary(BinaryExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitList(ListExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitListComprehension(ListComprehensionExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitSpread(SpreadExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitPipeline(PipelineExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitMatch(MatchExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitSeq(SeqExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitIf(IfExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitNode(NodeExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitRecord(RecordExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitRecordProperty(RecordPropertyExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitWildcardPattern(WildcardPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitLiteralPattern(LiteralPatternExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitLoadLocal(LoadLocalExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitStoreLocal(StoreLocalExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}

auto ExprPrinter::VisitGetProperty(GetPropertyExpr* expr) -> VisitResult {
  return VisitResult::kContinue;
}
}  // namespace kura::expr
