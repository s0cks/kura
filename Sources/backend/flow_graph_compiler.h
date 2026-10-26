#ifndef KURA_FLOW_GRAPH_COMPILER_H
#define KURA_FLOW_GRAPH_COMPILER_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <string>
#include <vector>

#include "flow_graph.h"
#include "module.h"

namespace kura {
class FlowGraphCompiler {
 private:
  std::unique_ptr<llvm::LLVMContext>& context_;
  std::unique_ptr<llvm::Module>& module_;
  bool optimize_;

 public:
  explicit FlowGraphCompiler(std::unique_ptr<llvm::LLVMContext>& ctx, std::unique_ptr<llvm::Module>& module,
                             bool optimize = true) :
    context_(ctx),
    module_(module),
    optimize_(optimize) {}
  ~FlowGraphCompiler() = default;

  auto Compile(Module* m, ModuleGraph* graphs, std::vector<std::string>& errors) -> bool;
};
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_COMPILER_H
