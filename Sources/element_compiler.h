#ifndef KURA_ELEMENT_COMPILER_H
#define KURA_ELEMENT_COMPILER_H

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "common.h"
#include "element.h"

namespace kura::elem {

#define FOR_EACH_YGFUNC(V)          \
  V(set_width, YGNodeStyleSetWidth) \
  V(set_height, YGNodeStyleSetHeight)

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
  llvm::FunctionCallee new_node_func_{};

  // malloc
  llvm::FunctionType* malloc_func_type_ = nullptr;
  llvm::FunctionCallee malloc_func_{};

#define DECLARE_YGFUNC(Name, Func)                 \
  llvm::FunctionType* Name##_func_type_ = nullptr; \
  llvm::FunctionCallee Name##_func_{};

  FOR_EACH_YGFUNC(DECLARE_YGFUNC)
#undef DECLARE_YGFUNC
  void InitYogaFunctions(llvm::Module* m);
  void InitMallocFunction(llvm::Module* m);

  inline void SetValueWithSetter(llvm::Value* this_ptr, llvm::FunctionCallee setter, const float value) {
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

  inline void SetWidth(llvm::Value* node, const float value) {
    return SetValueWithSetter(node, set_width_func_, value);
  }

  inline void SetHeight(llvm::Value* node, const float value) {
    return SetValueWithSetter(node, set_height_func_, value);
  }

  inline void CreateConstructor(llvm::Module* m, llvm::Type* ret_type,
                                const llvm::SmallVector<llvm::Type*>& param_types, llvm::StructType* struct_type,
                                llvm::FunctionType** ctor_type, llvm::Function** ctor, llvm::BasicBlock** entry,
                                llvm::Value** this_ptr) {
    const auto new_ctor_type = llvm::FunctionType::get(ret_type, param_types, false);
    if (!new_ctor_type)
      std::runtime_error("failed to generate new_ctor_type");
    const auto new_ctor = llvm::Function::Create(new_ctor_type, llvm::Function::ExternalLinkage, "Test_Constructor", m);
    if (!new_ctor)
      std::runtime_error("failed to generate new ctor function");
    const auto new_entry = llvm::BasicBlock::Create(*ctx_, "constructor", new_ctor);
    if (!new_entry)
      std::runtime_error("failed to generate new basic block for ctor function");
    builder_.SetInsertPoint(new_entry);
    const auto new_this_ptr = builder_.CreateStructGEP(struct_type, new_ctor->getArg(0), 1, "this");
    if (!new_this_ptr)
      std::runtime_error("failed to create new this ptr for ctor function");
    new_this_ptr->setName("this");

    (*ctor_type) = new_ctor_type;
    (*ctor) = new_ctor;
    (*entry) = new_entry;
    (*this_ptr) = new_this_ptr;
  }

 public:
  ElementCompiler(llvm::LLVMContext* ctx);
  ~ElementCompiler() = default;

  auto Compile() -> std::unique_ptr<llvm::Module>;
};
}  // namespace kura::elem

#endif  // KURA_ELEMENT_COMPILER_H
