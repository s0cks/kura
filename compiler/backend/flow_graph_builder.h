#ifndef KURA_FLOW_GRAPH_BUILDER_H
#define KURA_FLOW_GRAPH_BUILDER_H

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "common.h"
#include "dom/node.h"
#include "frontend/expr.h"

namespace kura {
class Module;
class Function;
class LocalVariable;

template <class I>
concept HasBlockId = requires(I instr, const uint64_t id) {
  { instr.SetBlockId(id) } -> std::same_as<void>;
  { instr.GetBlockId() } -> std::same_as<BlockId>;
};

class FlowGraphBuilder {
  friend class EffectVisitor;
  friend class ValueVisitor;
  friend class NodeVisitor;
  friend class DomLowering;

 public:
  struct Context {
    ModuleGraph* graph = nullptr;
    std::unordered_map<std::string, Function*> functions{};
    uint64_t lambda_count = 0;
    std::vector<std::string> errors{};
    // where to dump the virtual DOM of each view; null: don't
    std::ostream* dump_dom = nullptr;
  };

 private:
  Context* ctx_;
  std::string name_;
  Function* function_;
  BlockId current_block_id_ = 0;
  GraphEntryInstr* entry_ = nullptr;
  TargetEntryInstr* target_ = nullptr;
  FlowGraph* graph_ = nullptr;

  EntryInstr* block_ = nullptr;
  Instruction* tail_ = nullptr;

  std::unordered_map<LocalVariable*, Definition*> env_{};

  FlowGraphBuilder(Context* ctx, const std::string& name, Function* function);

  template <HasBlockId I = TargetEntryInstr>
  inline auto CreateNewBlock() -> I* {
    const auto entry = I::New();
    entry->SetBlockId(current_block_id_++);
    return entry;
  }

  inline auto NewTarget(const std::string& label) -> TargetEntryInstr* {
    const auto target = CreateNewBlock<TargetEntryInstr>();
    target->SetLabel(String::New(label + "." + std::to_string(target->GetBlockId())));
    return target;
  }

  inline auto NewJoin(const std::string& label) -> JoinEntryInstr* {
    const auto join = CreateNewBlock<JoinEntryInstr>();
    join->SetLabel(String::New(label + "." + std::to_string(join->GetBlockId())));
    return join;
  }

  void Emit(Instruction* instr);
  auto Bind(Definition* defn) -> Value*;
  void Terminate(Instruction* instr);
  void Enter(EntryInstr* block);

  inline auto IsClosed() const -> bool {
    return tail_ == nullptr;
  }

  void Define(LocalVariable* local, Definition* defn) {
    env_[local] = defn;
  }

  auto Lookup(LocalVariable* local) const -> Definition*;
  auto FindFunction(LocalVariable* local) const -> Function*;

  auto Fail(const std::string& message) -> VisitResult;

  // The constant `local` holds in this graph, or null if it isn't one.
  auto FoldableLocal(LocalVariable* local) const -> Object*;

  // Records that `value` has to have a type `def` accepts; the type analysis
  // checks it.
  void AddPropertyConstraint(Value* value, const dom::PropertyDef* def, const std::string& where);

  // Writes the virtual DOM under `root` to `Context::dump_dom`, if it is set.
  void DumpDom(dom::Node* root) const;

  auto BuildBody(expr::Expr* body) -> bool;
  auto BuildViewBody(expr::Expr* body) -> bool;
  auto BuildInit(const std::vector<expr::StoreLocalExpr*>& bindings) -> bool;
  auto Finish() -> FlowGraph*;

  void AddParameters(const std::vector<LocalVariable*>& params, const std::vector<LocalVariable*>& captures);

 public:
  ~FlowGraphBuilder() = default;

  static auto IsGlobal(LocalVariable* local) -> bool;
  static auto BuildLambda(Context* ctx, expr::LambdaExpr* lambda, const std::vector<LocalVariable*>& captures)
      -> FlowGraph*;

 public:
  static auto BuildModuleGraph(Module* module, std::vector<std::string>* errors = nullptr,
                               std::ostream* dump_dom = nullptr) -> ModuleGraph*;

  static auto BuildFlowGraph(expr::SeqExpr* expr) -> FlowGraph*;
};

auto CollectCaptures(expr::LambdaExpr* lambda) -> std::vector<LocalVariable*>;
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_BUILDER_H
