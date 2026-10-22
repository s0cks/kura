#ifndef KURA_BACKEND_VALUE_VISITOR_H
#define KURA_BACKEND_VALUE_VISITOR_H

#include "backend/effect_visitor.h"

namespace kura {
class ValueVisitor : public EffectVisitor {
 private:
  Value* value_ = nullptr;

 protected:
  void ReturnValue(Value* value) override {
    value_ = value;
  }

  void ReturnDefinition(Definition* defn) override {
    return ReturnValue(Bind(defn));
  }

  inline void ReturnNull() {
    // TODO(@s0cks): get cached null instance
    return ReturnDefinition(ConstantInstr::New(nullptr));
  }

  inline auto CreateLoad(Value* instance, Property* property) -> Value* {
    return Bind(LoadPropertyInstr::New(instance, property));
  }

  inline auto CreateSafeLoad(Value* instance, Property* property) -> Value* {
    return Bind(LoadPropertySafelyInstr::New(instance, property));
  }

 public:
  explicit ValueVisitor(FlowGraphBuilder* owner) :
    EffectVisitor(owner) {}
  ~ValueVisitor() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  inline auto HasValue() const -> bool {
    return GetValue() != nullptr;
  }

  auto VisitGetProperty(expr::GetPropertyExpr* expr) -> VisitResult override;
  auto VisitIf(expr::IfExpr* expr) -> VisitResult override;
  auto VisitSeq(expr::SeqExpr* expr) -> VisitResult override;
  auto VisitCall(expr::CallExpr* expr) -> VisitResult override;
  auto VisitLiteral(expr::LiteralExpr* expr) -> VisitResult override;
  auto VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult override;
  auto VisitUnary(expr::UnaryExpr* expr) -> VisitResult override;
  auto VisitBinary(expr::BinaryExpr* expr) -> VisitResult override;
  auto VisitRecord(expr::RecordExpr* expr) -> VisitResult override;
  auto VisitList(expr::ListExpr* expr) -> VisitResult override;
  auto VisitLiteralPattern(expr::LiteralPatternExpr* expr) -> VisitResult override;
  auto VisitWildcardPattern(expr::WildcardPatternExpr* expr) -> VisitResult override;
  auto VisitMatch(expr::MatchExpr* expr) -> VisitResult override;

  inline auto operator()(expr::Expr* expr) -> VisitResult {
    return expr ? expr->Accept(this) : VisitResult::Stop();
  }

  operator Value*() const {
    return GetValue();
  }
};
}  // namespace kura

#endif  // KURA_BACKEND_VALUE_VISITOR_H
