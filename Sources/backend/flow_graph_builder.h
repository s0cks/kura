#ifndef KURA_FLOW_GRAPH_BUILDER_H
#define KURA_FLOW_GRAPH_BUILDER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "common.h"
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

// Builds the flow graph of one function body.
//
// The builder owns a cursor: the block instructions are currently appended to
// (`block_`) and the last instruction of it (`tail_`). The expression visitors
// (EffectVisitor, ValueVisitor) lower an Expr by emitting at the cursor, and
// control flow moves the cursor between blocks (`Enter`).
//
// Locals are resolved while building, like in Dart's graph builder: the
// environment maps each local to the definition that currently holds its value,
// so a LoadLocal/StoreLocal of a function-level local never reaches the graph.
// Phis are only needed where a value is loop-carried, which the loop lowerings
// create explicitly. Module-level bindings are the exception: they live in
// memory, written by the module initializer, so they stay StoreLocal/LoadLocal.
class FlowGraphBuilder {
  friend class EffectVisitor;
  friend class ValueVisitor;

 public:
  // State shared by the graphs of one module.
  struct Context {
    ModuleGraph* graph = nullptr;
    std::unordered_map<std::string, Function*> functions{};
    uint64_t lambda_count = 0;
    std::vector<std::string> errors{};
  };

 private:
  Context* ctx_;
  std::string name_;
  Function* function_;
  BlockId current_block_id_ = 0;
  GraphEntryInstr* entry_ = nullptr;
  TargetEntryInstr* target_ = nullptr;
  FlowGraph* graph_ = nullptr;

  // the cursor
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

  // Appends `instr` to the current block.
  void Emit(Instruction* instr);

  // Appends `defn` and returns a use of it.
  auto Bind(Definition* defn) -> Value*;

  // Emits `instr` as the end of the current block; the cursor is then closed
  // until the next `Enter`.
  void Terminate(Instruction* instr);

  // Continues at the start of `block`.
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

  // Lowers `body` and ends the graph with a Return of its value.
  auto BuildBody(expr::Expr* body) -> bool;
  auto BuildViewBody(expr::Expr* body) -> bool;
  auto BuildInit(const std::vector<expr::StoreLocalExpr*>& bindings) -> bool;
  auto Finish() -> FlowGraph*;

  void AddParameters(const std::vector<LocalVariable*>& params, const std::vector<LocalVariable*>& captures);

 public:
  ~FlowGraphBuilder() = default;

  // Module-level locals (top-level bindings and function names) live in the
  // root scope.
  static auto IsGlobal(LocalVariable* local) -> bool;

  // Lowers a closure body into its own graph and registers it with the module.
  static auto BuildLambda(Context* ctx, expr::LambdaExpr* lambda, const std::vector<LocalVariable*>& captures)
      -> FlowGraph*;

 public:
  // Builds a graph for each function of `module`, the lambdas inside them, and
  // an initializer for the top-level bindings. Returns null (and fills `errors`)
  // if something can't be lowered.
  static auto BuildModuleGraph(Module* module, std::vector<std::string>* errors = nullptr) -> ModuleGraph*;

  // Builds a stand-alone graph for `expr`, which has no parameters and can't
  // refer to module functions.
  static auto BuildFlowGraph(expr::SeqExpr* expr) -> FlowGraph*;
};

// The locals of enclosing functions that `lambda` reads, in order of first use.
auto CollectCaptures(expr::LambdaExpr* lambda) -> std::vector<LocalVariable*>;
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_BUILDER_H
