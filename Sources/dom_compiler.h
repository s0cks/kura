#ifndef KURA_DOM_COMPILER_H
#define KURA_DOM_COMPILER_H

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "common.h"
#include "dom.h"

namespace kura::dom {
class DOMCompiler {
  DEFINE_NON_COPYABLE_TYPE(DOMCompiler);

 private:
  llvm::LLVMContext* ctx_;
  llvm::IRBuilder<> builder_;

  llvm::Type* ret_type_ = nullptr;
  llvm::Type* ptr_type_ = nullptr;
  llvm::Type* float_type_ = nullptr;

  // YGNodeNew
  llvm::FunctionType* new_node_func_type_ = nullptr;
  llvm::FunctionType* set_width_func_type_ = nullptr;
  llvm::FunctionType* set_height_func_type_ = nullptr;

  // clang-format off
#define DEFINE_COMPILE_PROPERTY(Name) \
  void Compile##Name##Property(llvm::Value* node_ref, Name##Property* prop);
  FOR_EACH_DOM_PROPERTY(DEFINE_COMPILE_PROPERTY)
#undef DEFINE_COMPILE_PROPERTY
  // clang-format on

  inline void SetValueWithSetter(llvm::FunctionCallee setter, llvm::Value* this_ptr, const float value) {
    std::vector<llvm::Value*> args = {
        this_ptr,
        llvm::ConstantFP::get(float_type_, value),
    };
    builder_.CreateCall(setter, args);
  }

  inline auto CreateNewYGNode(llvm::FunctionCallee ctor, const std::string_view name) -> llvm::Value* {
    return builder_.CreateCall(ctor, {}, name);
  }

 public:
  DOMCompiler(llvm::LLVMContext* ctx);
  ~DOMCompiler();

  auto Compile() -> std::unique_ptr<llvm::Module>;

 public:
  static void Init();
};
}  // namespace kura::dom

#endif  // KURA_DOM_COMPILER_H
