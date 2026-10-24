#ifndef KURA_LLVM_CODEGEN_H
#define KURA_LLVM_CODEGEN_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>

#include "backend/flow_graph.h"

namespace kura {
class Module;

// Lowers a type checked module to LLVM IR.
//
// Each graph becomes a function that takes and returns its values in the
// representation of their static types: `Int` is an i64, `Float` and the units
// are doubles, `Bool` is an i1, `None` is an i8, and a string, seq, record,
// node or function is a pointer to a runtime object. `Number`, `Any`, unions
// and variants are a `{ i64 tag, i64 payload }` pair (see runtime_abi.h). A value
// is converted at every point it flows into a place of a different type: an
// argument, a return, a phi, a store.
//
// Besides the typed function each graph gets a thunk that takes and returns
// tagged values: that's what a closure holds, so a function of any signature
// can be called through a function value, and it's the entry point for a host.
// A module function `name` is exported as `kura_fn_<name>` (with `-` as `_`).
// Run `kura_module_init` once before anything else.
//
// The module's IR is valid on return, or the result is null and `error` says why.
auto GenerateLLVM(llvm::LLVMContext& context, Module* module, ModuleGraph* graphs, std::string* error)
    -> std::unique_ptr<llvm::Module>;

// Runs LLVM's optimization pipeline over the module.
void OptimizeLLVM(llvm::Module& module);

// The exported name of the entry point for a module function.
auto EntryPointName(const std::string& function) -> std::string;
}  // namespace kura

#endif  // KURA_LLVM_CODEGEN_H
