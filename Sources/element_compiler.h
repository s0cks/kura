#ifndef KURA_ELEMENT_COMPILER_H
#define KURA_ELEMENT_COMPILER_H

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "common.h"
#include "element.h"

namespace kura::elem {
class ElementCompiler {
  DEFINE_NON_COPYABLE_TYPE(ElementCompiler);

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

  inline auto GetYogaSetterFunctionType(llvm::Type* rhs) -> llvm::FunctionType* {
    llvm::SmallVector<llvm::Type*> params = {
        ptr_type_,
        rhs,
    };
    return llvm::FunctionType::get(builder_.getVoidTy(), params, false);
  }

 public:
  ElementCompiler(llvm::LLVMContext* ctx);
  ~ElementCompiler();

  auto Compile() -> std::unique_ptr<llvm::Module>;

 public:
  static void Init();
};
}  // namespace kura::elem

#endif  // KURA_ELEMENT_COMPILER_H
