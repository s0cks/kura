#include "frontend/expr_printer.h"

#include "local_scope.h"

namespace kura::expr {
void ExprPrinter::PrintChild(Expr* expr) {
  if (expr == nullptr) {
    stream() << "<null>";
    return;
  }
  expr->Accept(this);
}

void ExprPrinter::PrintChildren(const ExprList& exprs) {
  for (const auto expr : exprs) {
    stream() << ' ';
    PrintChild(expr);
  }
}

void ExprPrinter::PrintDirectives(const DirectiveList& directives) {
  for (const auto directive : directives) {
    stream() << " @" << directive->name;
    if (directive->args.empty())
      continue;
    stream() << '(';
    for (size_t i = 0; i < directive->args.size(); i++) {
      stream() << (i ? ", " : "");
      PrintChild(directive->args[i]);
    }
    stream() << ')';
  }
}

static inline auto NameOf(LocalVariable* local) -> const std::string& {
  return local->GetName()->GetValue();
}

auto ExprPrinter::VisitLiteral(LiteralExpr* expr) -> VisitResult {
  stream() << expr->GetValue()->ToString();
  return true;
}

auto ExprPrinter::VisitUnary(UnaryExpr* expr) -> VisitResult {
  stream() << "(unary " << expr->GetOp() << ' ';
  PrintChild(expr->GetChildAt(0));
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitBinary(BinaryExpr* expr) -> VisitResult {
  stream() << "(binary " << expr->GetOp() << ' ';
  PrintChild(expr->GetChildAt(0));
  stream() << ' ';
  PrintChild(expr->GetChildAt(1));
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitSeq(SeqExpr* expr) -> VisitResult {
  stream() << "(seq";
  PrintChildren(expr->GetChildren());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitIf(IfExpr* expr) -> VisitResult {
  stream() << "(if ";
  PrintChild(expr->GetCondition());
  stream() << ' ';
  PrintChild(expr->GetThen());
  stream() << ' ';
  PrintChild(expr->GetElse());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitCase(CaseExpr* expr) -> VisitResult {
  stream() << "(case ";
  PrintChild(expr->GetPattern());
  stream() << " => ";
  PrintChild(expr->GetBody());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitMatch(MatchExpr* expr) -> VisitResult {
  stream() << "(match ";
  PrintChild(expr->GetSubject());
  for (const auto& arm : expr->GetCases()) {
    stream() << ' ';
    PrintChild(arm);
  }
  if (expr->HasWildcard()) {
    stream() << " (case _ => ";
    PrintChild(expr->GetWildcard());
    stream() << ')';
  }
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitNode(NodeExpr* expr) -> VisitResult {
  stream() << "(#" << expr->GetKind();
  PrintDirectives(expr->GetDirectives());
  for (const auto& property : expr->GetProperties()) {
    stream() << ' ';
    PrintChild(property);
  }
  PrintChildren(expr->GetChildren());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitPipeline(PipelineExpr* expr) -> VisitResult {
  stream() << "(pipeline";
  PrintChildren(expr->GetChildren());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitList(ListExpr* expr) -> VisitResult {
  stream() << "(list";
  PrintChildren(expr->GetChildren());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitListComprehension(ListComprehensionExpr* expr) -> VisitResult {
  stream() << "(for " << expr->GetBinding() << " in ";
  PrintChild(expr->GetIterable());
  if (expr->HasClause()) {
    stream() << " if ";
    PrintChild(expr->GetClause());
  }
  stream() << " => ";
  PrintChild(expr->GetBody());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitFold(FoldExpr* expr) -> VisitResult {
  stream() << "(fold " << NameOf(expr->GetBinding()) << " in ";
  PrintChild(expr->GetIterable());
  if (expr->HasClause()) {
    stream() << " if ";
    PrintChild(expr->GetClause());
  }
  stream() << ' ' << NameOf(expr->GetAccumulator()) << " = ";
  PrintChild(expr->GetInitial());
  stream() << " => ";
  PrintChild(expr->GetBody());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitLambda(LambdaExpr* expr) -> VisitResult {
  stream() << "(lambda (";
  for (size_t i = 0; i < expr->GetParams().size(); i++)
    stream() << (i ? " " : "") << NameOf(expr->GetParams()[i]);
  stream() << ") ";
  PrintChild(expr->GetBody());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitVariant(VariantExpr* expr) -> VisitResult {
  stream() << "(variant " << expr->GetVariantName();
  if (expr->HasPayload()) {
    stream() << ' ';
    PrintChild(expr->GetPayload());
  }
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitRecord(RecordExpr* expr) -> VisitResult {
  stream() << "(record";
  PrintChildren(expr->GetEntries());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitSpread(SpreadExpr* expr) -> VisitResult {
  stream() << "...";
  PrintChild(expr->GetValue());
  return true;
}

auto ExprPrinter::VisitStoreProperty(StorePropertyExpr* expr) -> VisitResult {
  stream() << expr->GetProperty()->GetPropertyName()->GetValue() << ": ";
  PrintChild(expr->GetValue());
  return true;
}

auto ExprPrinter::VisitGetProperty(GetPropertyExpr* expr) -> VisitResult {
  stream() << "(get ";
  PrintChild(expr->GetChildAt(0));
  stream() << ' ' << expr->GetProperty()->GetPropertyName()->GetValue() << ')';
  return true;
}

auto ExprPrinter::VisitLoadLocal(LoadLocalExpr* expr) -> VisitResult {
  stream() << NameOf(expr->GetLocal());
  return true;
}

auto ExprPrinter::VisitStoreLocal(StoreLocalExpr* expr) -> VisitResult {
  stream() << "(set " << NameOf(expr->GetLocal());
  if (expr->GetAnnotation())
    stream() << ": " << expr->GetAnnotation()->ToString();
  PrintDirectives(expr->GetDirectives());
  stream() << ' ';
  PrintChild(expr->GetValue());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitCall(CallExpr* expr) -> VisitResult {
  stream() << "(call ";
  PrintChild(expr->GetTarget());
  PrintChildren(expr->GetArgs());
  stream() << ')';
  return true;
}

auto ExprPrinter::VisitWildcardPattern(WildcardPatternExpr* expr) -> VisitResult {
  stream() << '_';
  return true;
}

auto ExprPrinter::VisitLiteralPattern(LiteralPatternExpr* expr) -> VisitResult {
  PrintChild(expr->GetLiteral());
  return true;
}

auto ExprPrinter::VisitBindingPattern(BindingPatternExpr* expr) -> VisitResult {
  stream() << "(bind " << NameOf(expr->GetLocal()) << ')';
  return true;
}

auto ExprPrinter::VisitVariantPattern(VariantPatternExpr* expr) -> VisitResult {
  stream() << "(variant-pattern " << expr->GetVariantName();
  if (expr->HasPayload()) {
    stream() << ' ';
    PrintChild(expr->GetPayload());
  }
  stream() << ')';
  return true;
}
}  // namespace kura::expr
