#ifndef KURA_FLOW_GRAPH_BUILDER_H
#define KURA_FLOW_GRAPH_BUILDER_H

#include "common.h"
#include "expr.h"
#include "ir.h"

namespace kura {
class FlowGraph;
class FlowGraphBuilder {
 private:
  TargetEntryInstr* target_;
  GraphEntryInstr* graph_;

 public:
  FlowGraphBuilder();
  ~FlowGraphBuilder() = default;

  auto Visit(expr::Expr* expr) -> VisitResult;
  auto Build() -> FlowGraph*;

  inline auto operator()(expr::Expr* expr) -> VisitResult {
    return Visit(expr);
  }

 public:
  static inline auto BuildFlowGraph(expr::Expr* expr) -> FlowGraph* {
    if (!expr)
      return nullptr;

    FlowGraphBuilder builder{};
    const auto result = builder(expr);
    if (!result) {
      std::cerr << "failed to build flow graph: " << result;
      return nullptr;
    }

    return builder.Build();
  }
};

class ValueVisitor;
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

  inline void AddGoto(EntryInstr* instr) {
    Add(GotoInstr::New(instr));
  }

  inline void AddStoreProperty(Value* record, Property* property, Value* value) {
    return Add(StorePropertyInstr::New(record, property, value));
  }

  inline void AddStoreProperty(Value* record, Property* property, ValueVisitor& vis);

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

  virtual auto AddBranch(Value* condition, TargetEntryInstr* then_expr, TargetEntryInstr* else_expr)
      -> JoinEntryInstr* {
    const auto join = JoinEntryInstr::New();
    Add(BranchInstr::New(condition, then_expr, else_expr, join));
    return join;
  }

  auto ProcessValueList(const expr::ExprList& expressions, ValueList& values) -> VisitResult;
  auto ProcessValueList(expr::DynamicTemplateExpr& expression, ValueList& values)
      -> VisitResult;  // TODO(@s0cks): add const to expression parameter

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

class ValueVisitor : public EffectVisitor {
 private:
  Value* value_ = nullptr;

 protected:
  void ReturnValue(Value* value) override {
    value_ = value;
  }

  void ReturnDefinition(Definition* defn) override {
    value_ = Bind(defn);
  }

  inline void ReturnNull() {
    // TODO(@s0cks): get cached null instance
    return ReturnDefinition(ConstantInstr::New(nullptr));
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

  auto VisitCall(expr::CallExpr* expr) -> VisitResult override;
  auto VisitLiteral(expr::LiteralExpr* expr) -> VisitResult override;
  auto VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult override;
  auto VisitUnary(expr::UnaryExpr* expr) -> VisitResult override;
  auto VisitBinary(expr::BinaryExpr* expr) -> VisitResult override;
  auto VisitRecord(expr::RecordExpr* expr) -> VisitResult override;
  auto VisitList(expr::ListExpr* expr) -> VisitResult override;

  inline auto operator()(expr::Expr* expr) -> VisitResult {
    return expr ? expr->Accept(this) : VisitResult::Stop();
  }

  operator Value*() const {
    return GetValue();
  }
};

void EffectVisitor::AddStoreProperty(Value* record, Property* property, ValueVisitor& vis) {
  return AddStoreProperty(record, property, vis.GetValue());
}
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_BUILDER_H
