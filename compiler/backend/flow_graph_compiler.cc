#include "flow_graph_compiler.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>

#include "backend/escape_analysis.h"
#include "backend/flow_graph.h"
#include "backend/llvm_codegen.h"
#include "backend/purity_analysis.h"
#include "backend/runtime_abi.h"
#include "backend/type_analysis.h"

namespace kura {
auto FlowGraphCompiler::Compile(Module* m, ModuleGraph* graphs, std::vector<std::string>& errors) -> bool {
  if (!graphs || !TypeAnalysis::Run(m, graphs, &errors) || !PurityAnalysis::Run(m, graphs, &errors))
    return false;

  for (FlowGraph* graph : graphs->GetGraphs()) {
    if (graph->SimplifyCFG())
      graph->RebuildCFG();
    graph->EliminateDeadCode();
    graph->OptimizeCOWUpdates();
  }

  EscapeAnalysis::Run(graphs);

  for (FlowGraph* graph : graphs->GetGraphs()) {
    if (graph == graphs->GetInit())
      continue;
    graph->InsertARC();
  }

  context_ = std::make_unique<llvm::LLVMContext>();
  std::string error;
  module_ = GenerateLLVM(*context_, m, graphs, &error);
  if (!module_) {
    errors.push_back(error);
    return false;
  }

  if (optimize_)
    OptimizeLLVM(*module_);

  return true;
}
}  // namespace kura
