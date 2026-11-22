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
class Jit {
 private:
  std::unique_ptr<llvm::orc::LLJIT> jit_;

  explicit Jit(std::unique_ptr<llvm::orc::LLJIT> jit) :
    jit_(std::move(jit)) {}

 public:
  ~Jit() = default;

  auto LoadLibrary(const std::string& path, std::string* error) -> bool;
  auto Add(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context, std::string* error)
      -> bool;
  auto Initialize(std::string* error) -> bool;
  auto RunTests(const char* filter, bool fail_fast, uint64_t* failed, std::string* error) -> bool;
  auto RunBenchmarks(const char* filter, uint64_t* over_budget, std::string* error) -> bool;
  auto Call(const std::string& function, const std::vector<rt::KuraValue>& args, rt::KuraValue* result,
            std::string* error) -> bool;

 public:
  static auto Create(std::string* error) -> std::unique_ptr<Jit>;
};
}  // namespace kura

#endif  // KURA_JIT_H
