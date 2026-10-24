#ifndef KURA_JIT_H
#define KURA_JIT_H

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>
#include <vector>

#include "backend/runtime_abi.h"

namespace kura {
// Runs generated code in this process: the runtime library's functions are the
// ones compiled code calls.
class Jit {
 private:
  std::unique_ptr<llvm::orc::LLJIT> jit_;

  explicit Jit(std::unique_ptr<llvm::orc::LLJIT> jit) :
    jit_(std::move(jit)) {}

 public:
  ~Jit() = default;

  // Takes ownership of the module and the context it lives in.
  auto Add(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context, std::string* error)
      -> bool;

  // Runs the module's initializer: registers its names and strings and
  // evaluates its top-level bindings. Do this once, before any call.
  auto Initialize(std::string* error) -> bool;

  // Calls the module function `function`.
  auto Call(const std::string& function, const std::vector<rt::KuraValue>& args, rt::KuraValue* result,
            std::string* error) -> bool;

 public:
  static auto Create(std::string* error) -> std::unique_ptr<Jit>;
};
}  // namespace kura

#endif  // KURA_JIT_H
