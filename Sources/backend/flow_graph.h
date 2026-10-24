#ifndef KURA_FLOW_GRAPH_H
#define KURA_FLOW_GRAPH_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir.h"

namespace kura {
class Function;
class LocalVariable;
class CompileType;

// The flow graph of a single function (or lambda, or the module initializer).
//
// It starts at a GraphEntryInstr whose only successor is the first block.
// Blocks are chains of instructions that start with an EntryInstr and end in a
// Goto, Branch, or Return. `Finalize()` discovers the blocks reachable from the
// entry and fills in the CFG edges, which every analysis (including the type
// pass) iterates.
class FlowGraph {
 public:
  using BlockList = std::vector<EntryInstr*>;

 private:
  std::string name_;
  Function* function_;
  GraphEntryInstr* entry_;
  std::vector<ParameterInstr*> parameters_{};
  std::vector<LocalVariable*> captures_{};
  std::vector<CompileType*> parameter_types_{};
  CompileType* return_type_ = nullptr;
  std::unordered_set<EntryInstr*> dead_{};
  BlockList preorder_{};
  BlockList postorder_{};
  BlockList reverse_postorder_{};
  std::unordered_map<EntryInstr*, EntryInstr*> idom_{};

  void ComputeDominators();

  FlowGraph(const std::string& name, Function* function, GraphEntryInstr* entry) :
    name_(name),
    function_(function),
    entry_(entry) {}

  friend class FlowGraphBuilder;
  inline void AddParameter(ParameterInstr* param) {
    parameters_.push_back(param);
  }

  inline void SetCaptures(std::vector<LocalVariable*> captures) {
    captures_ = std::move(captures);
  }

 public:
  ~FlowGraph() = default;

  // Unique within a ModuleGraph.
  auto GetName() const -> const std::string& {
    return name_;
  }

  // The module function this graph was built from; null for lambdas and the
  // module initializer.
  auto GetFunction() const -> Function* {
    return function_;
  }

  auto GetEntry() const -> GraphEntryInstr* {
    return entry_;
  }

  inline auto HasEntry() const -> bool {
    return GetEntry() != nullptr;
  }

  auto GetParameters() const -> const std::vector<ParameterInstr*>& {
    return parameters_;
  }

  auto GetNumberOfParameters() const -> uint64_t {
    return parameters_.size();
  }

  // Locals of the enclosing function that a lambda closes over. Closure
  // creation passes their values in this order.
  auto GetCaptures() const -> const std::vector<LocalVariable*>& {
    return captures_;
  }

  inline auto IsLambda() const -> bool {
    return !captures_.empty() || is_lambda_;
  }

  // The declared (or inferred) types of the parameters, captures last, and of
  // the result. Set by the type analysis; empty/null before it.
  auto GetParameterTypes() const -> const std::vector<CompileType*>& {
    return parameter_types_;
  }

  auto GetReturnType() const -> CompileType* {
    return return_type_;
  }

  void SetSignature(std::vector<CompileType*> parameters, CompileType* result) {
    parameter_types_ = std::move(parameters);
    return_type_ = result;
  }

  // Blocks the type analysis proved can't run (the code after a match whose arms
  // cover every variant). Their instructions have no types, so code generation
  // skips them.
  auto IsDead(EntryInstr* block) const -> bool {
    return dead_.contains(block);
  }

  void SetDeadBlocks(std::unordered_set<EntryInstr*> dead) {
    dead_ = std::move(dead);
  }

  // Blocks reachable from the entry; the entry is first in preorder and
  // reverse postorder.
  auto GetPreorder() const -> const BlockList& {
    return preorder_;
  }

  auto GetPostorder() const -> const BlockList& {
    return postorder_;
  }

  auto GetReversePostorder() const -> const BlockList& {
    return reverse_postorder_;
  }

  // Computes the block orders and the successor/predecessor edges. Call it
  // once, after the last instruction has been linked.
  void Finalize();

  // The immediate dominator of `block` (null for the entry). Valid after
  // Finalize().
  auto GetImmediateDominator(EntryInstr* block) const -> EntryInstr*;

  // Does `a` dominate `b`? Every block dominates itself.
  auto Dominates(EntryInstr* a, EntryInstr* b) const -> bool;

  // Checks the invariants the analyses and code generation rely on: every
  // block ends in exactly one terminator, phis agree with their join's
  // predecessors, and every use is dominated by its definition. On failure
  // returns false and describes the first problem in `error`.
  auto Verify(std::string* error) const -> bool;

  // The instruction that ends `block` (Goto, Branch, or Return).
  static auto GetTerminator(EntryInstr* block) -> Instruction*;

 private:
  bool is_lambda_ = false;

 public:
  static inline auto New(const std::string& name, Function* function, GraphEntryInstr* entry) -> FlowGraph* {
    return new FlowGraph(name, function, entry);
  }
};

// Every flow graph of a module: its functions, the lambdas lowered out of them,
// and the initializer that evaluates top-level bindings.
class ModuleGraph {
 private:
  std::vector<FlowGraph*> graphs_{};
  std::unordered_map<Function*, FlowGraph*> by_function_{};
  FlowGraph* init_ = nullptr;
  std::unordered_map<LocalVariable*, CompileType*> global_types_{};

 public:
  ModuleGraph() = default;
  ~ModuleGraph() = default;

  auto GetGraphs() const -> const std::vector<FlowGraph*>& {
    return graphs_;
  }

  auto GetInit() const -> FlowGraph* {
    return init_;
  }

  auto GetGraphFor(Function* function) const -> FlowGraph* {
    const auto it = by_function_.find(function);
    return it == by_function_.end() ? nullptr : it->second;
  }

  inline void Add(FlowGraph* graph) {
    graphs_.push_back(graph);
    if (graph->GetFunction())
      by_function_[graph->GetFunction()] = graph;
  }

  // The type of a module-level binding, set by the type analysis.
  auto GetGlobalType(LocalVariable* local) const -> CompileType* {
    const auto it = global_types_.find(local);
    return it == global_types_.end() ? nullptr : it->second;
  }

  auto GetGlobalTypes() const -> const std::unordered_map<LocalVariable*, CompileType*>& {
    return global_types_;
  }

  inline void SetGlobalType(LocalVariable* local, CompileType* type) {
    global_types_[local] = type;
  }

  inline void SetInit(FlowGraph* graph) {
    init_ = graph;
    Add(graph);
  }
};
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_H
