#ifndef KURA_BACKEND_EFFECT_VISITOR_H
#define KURA_BACKEND_EFFECT_VISITOR_H

#include "backend/flow_graph_builder.h"

namespace kura {
class ValueVisitor;

// Lowers an expression whose result isn't needed: a statement of a block.
//
// Every lowering lives here; a ValueVisitor differs only in that it keeps the
// result (`ReturnValue`) and that control flow joins with a phi. All emission
// goes through the owning FlowGraphBuilder's cursor, so visitors are cheap,
// short-lived objects and sub-expressions are evaluated with `Eval`.
class EffectVisitor : public expr::ExprVisitor {
  friend class FlowGraphBuilder;

 private:
  FlowGraphBuilder* owner_;

 protected:
  inline auto Fail(const std::string& message) -> VisitResult {
    return owner_->Fail(message);
  }

  // Whether the value of the expression being lowered is used.
  virtual auto WantsValue() const -> bool {
    return false;
  }

  // Called with the result of the expression. A value visitor keeps it.
  virtual void ReturnValue(Value* value) {}

  // Appends `defn`, then returns it.
  void ReturnDefinition(Definition* defn);

  // Lowers `expr` for its value; null if it couldn't be lowered.
  auto Eval(expr::Expr* expr) -> Value*;

  // Lowers `expr` as a statement.
  auto EvalForEffect(expr::Expr* expr) -> bool;

  // Lowers an arm of a control flow expression: for its value iff this
  // expression's value is wanted. `value` is null for effects.
  auto EvalArm(expr::Expr* expr, Value** value) -> bool;

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

  // Lowers `pattern` against `subject`: tests branch to `fail` when they don't
  // hold and fall through (in the current block) when they do, and bindings
  // are defined. Sets `irrefutable` if the pattern can't fail.
  auto LowerPattern(expr::PatternExpr* pattern, Value* subject, TargetEntryInstr* fail, bool* irrefutable) -> bool;
  auto LowerShortCircuit(expr::BinaryExpr* expr) -> VisitResult;

  // A loop over a Seq. `header` has the index phi; `body` runs once per element
  // with the element in `item`, and the loop ends in `exit`.
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
