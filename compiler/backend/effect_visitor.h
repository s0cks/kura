#ifndef KURA_BACKEND_EFFECT_VISITOR_H
#define KURA_BACKEND_EFFECT_VISITOR_H

#include "backend/flow_graph_builder.h"

namespace kura {
class ValueVisitor;

class EffectVisitor : public expr::ExprVisitor {
  friend class FlowGraphBuilder;
  friend class DomLowering;

 private:
  FlowGraphBuilder* owner_;

 protected:
  inline auto Fail(const std::string& message) -> VisitResult {
    return owner_->Fail(message);
  }

  virtual auto WantsValue() const -> bool {
    return false;
  }

  virtual void ReturnValue(Value* value) {}

  void ReturnDefinition(Definition* defn);
  auto Eval(expr::Expr* expr) -> Value*;
  auto EvalForEffect(expr::Expr* expr) -> bool;
  virtual auto EvalArm(expr::Expr* expr, Value** value) -> bool;

  auto EvalList(const expr::ExprList& exprs, ValueList* values) -> bool;

  inline auto Bind(Definition* defn) -> Value* {
    return owner_->Bind(defn);
  }

  inline void Emit(Instruction* instr) {
    owner_->Emit(instr);
  }

  inline auto NoneValue() -> Value* {
    return Bind(ConstantInstr::New(::kura::None::Get()));
  }

  auto LowerPattern(expr::PatternExpr* pattern, Value* subject, TargetEntryInstr* fail, bool* irrefutable) -> bool;
  auto LowerShortCircuit(expr::BinaryExpr* expr) -> VisitResult;

  struct Loop {
    Value* iterable = nullptr;
    Value* length = nullptr;
    PhiInstr* index = nullptr;
    JoinEntryInstr* header = nullptr;
    EntryInstr* preheader = nullptr;
    TargetEntryInstr* exit = nullptr;
    Value* item = nullptr;
  };

  auto BeginLoop(Value* iterable, Loop* loop) -> bool;
  void EndLoop(Loop* loop);

 public:
  explicit EffectVisitor(FlowGraphBuilder* owner) :
    expr::ExprVisitor(),
    owner_(owner) {}
  ~EffectVisitor() override = default;

  auto GetOwner() const -> FlowGraphBuilder* {
    return owner_;
  }

  // clang-format off
#define DEFINE_VISIT(Name) \
  auto Visit##Name(expr::Name##Expr*) -> VisitResult override;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on

  inline auto operator()(expr::Expr* expr) -> VisitResult {
    return expr ? expr->Accept(this) : VisitResult::Stop();
  }
};
}  // namespace kura

#endif  // KURA_BACKEND_EFFECT_VISITOR_H
