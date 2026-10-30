#ifndef KURA_LLVM_CODEGEN_H
#define KURA_LLVM_CODEGEN_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>

#include "backend/flow_graph.h"

namespace kura {
class Module;

auto GenerateLLVM(llvm::LLVMContext& context, Module* module, ModuleGraph* graphs, std::string* error)
    -> std::unique_ptr<llvm::Module>;
void OptimizeLLVM(llvm::Module& module);
auto EmitObjectFile(llvm::Module& module, const std::string& path, std::string* error) -> bool;
auto EntryPointName(const std::string& function) -> std::string;
}  // namespace kura

#endif  // KURA_LLVM_CODEGEN_H
