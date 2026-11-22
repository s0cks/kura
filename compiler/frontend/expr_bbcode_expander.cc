#include "compiler/frontend/expr_bbcode_expander.h"

#include "common.h"

namespace kura {
auto BBCodeExpander::Expand(expr::NodeExpr& rhs) -> VisitResult {
  return rhs.Accept(this);
}

#define VISIT_FN(Name) auto BBCodeExpander::Visit##Name(expr::Name##Expr* node)->VisitResult

VISIT_FN(Literal) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(MathCall) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Unary) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Binary) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Seq) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(If) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Case) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Match) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Node) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Pipeline) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(List) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(NewColor) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(CastColor) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(ListComprehension) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Record) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Spread) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(LoadLocal) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(StoreLocal) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(GetProperty) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Index) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(StoreProperty) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(WildcardPattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(LiteralPattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(BindingPattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(VariantPattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(RangePattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(RecordPattern) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Lambda) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Fold) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Variant) {
  // TODO(@s0cks): implement
  return Stop();
}

VISIT_FN(Call) {
  // TODO(@s0cks): implement
  return Stop();
}

}  // namespace kura
