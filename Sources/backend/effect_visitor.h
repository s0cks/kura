#ifndef KURA_BACKEND_EFFECT_VISITOR_H
#define KURA_BACKEND_EFFECT_VISITOR_H

#include "backend/flow_graph_builder.h"

namespace kura {
class EffectVisitor : public expr::ExprVisitor {
  friend class FlowGraphBuilder;

 private:
  FlowGraphBuilder* owner_;
  Instruction* entry_ = nullptr;
  Instruction* exit_ = nullptr;
  EntryInstr* block_ = nullptr;

 protected:
  inline void SetEntry(Instruction* instr) {
    entry_ = instr;
  }

  inline void SetExit(Instruction* instr) {
    exit_ = instr;
  }

  virtual void Do(Definition* defn) {
    if (IsEmpty()) {
      SetEntry(defn);
    } else {
      Instruction::Link(GetExit(), defn);
    }
    SetExit(defn);
  }

  virtual void ReturnDefinition(Definition* defn) {
    if (!defn->IsConstant())
      Do(defn);
  }

  void Add(Instruction* instr) {
    if (IsEmpty()) {
      SetEntry(instr);
      SetExit(instr);
    } else {
      Instruction::Link(GetExit(), instr);
      SetExit(instr);
    }
  }

#ifdef KURA_DEBUG

  inline void AddLabel(String* message) {
    return Add(LabelInstr::New(message));
  }

  inline void AddLabel(const std::string message) {
    return AddLabel(String::New(message));
  }

#endif  // KURA_DEBUG

  inline void AddGoto(EntryInstr* instr) {
    Add(GotoInstr::New(instr));
  }

  inline void AddStoreProperty(Value* record, Property* property, Value* value) {
    return Add(StorePropertyInstr::New(record, property, value));
  }

  void AddReturnExit(Value* value) {
    Add(ReturnInstr::New(value));
    exit_ = nullptr;
  }

  void AddReturnExit(Object* value) {
    return AddReturnExit(Bind(ConstantInstr::New(value)));
  }

  void Append(const EffectVisitor& rhs) {
    if (rhs.IsEmpty())
      return;

    if (IsEmpty()) {
      SetEntry(rhs.GetEntry());
    } else {
      Instruction::Link(GetExit(), rhs.GetEntry());
    }
    SetExit(rhs.GetExit());
  }

  auto Bind(Definition* defn) -> Value* {
    if (IsEmpty()) {
      SetEntry(defn);
    } else if (GetExit() != nullptr) {
      Instruction::Link(GetExit(), defn);
    }
    SetExit(defn);
    return Value::New(defn);
  }

  virtual void ReturnValue(Value* defn) {
    // do nothing
  }

  virtual void AddBranch(Value* condition, TargetEntryInstr* then_expr, JoinEntryInstr* join) {
    Add(BranchInstr::New(condition, then_expr, nullptr, join));
  }

  virtual void AddBranch(Value* condition, TargetEntryInstr* then_expr, TargetEntryInstr* else_expr,
                         JoinEntryInstr* join) {
    Add(BranchInstr::New(condition, then_expr, else_expr, join));
  }

  auto ProcessValueList(const expr::ExprList& expressions, ValueList& values) -> VisitResult;
  auto ProcessValueList(expr::DynamicTemplateExpr& expression, ValueList& values)
      -> VisitResult;  // TODO(@s0cks): add const to expression parameter

  static inline void AppendFragment(TargetEntryInstr* target, const EffectVisitor& vis) {
    Instruction::Link(target, vis.GetEntry());
  }

 public:
  explicit EffectVisitor(FlowGraphBuilder* owner) :
    expr::ExprVisitor(),
    owner_(owner) {}
  virtual ~EffectVisitor() = default;

  auto GetOwner() const -> FlowGraphBuilder* {
    return owner_;
  }

  auto GetEntry() const -> Instruction* {
    return entry_;
  }

  auto GetExit() const -> Instruction* {
    return exit_;
  }

  auto IsEmpty() const -> bool {
    return GetEntry() == nullptr;
  }

  auto IsOpen() const -> bool {
    return IsEmpty() || GetExit() != nullptr;
  }

  auto IsClosed() const -> bool {
    return !IsOpen();
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
