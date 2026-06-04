#include "element_compiler.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <yoga/YGNodeStyle.h>
#include <yoga/Yoga.h>

namespace kura::elem {
static llvm::StructType* BoxClassType = nullptr;
static llvm::PointerType* BoxPtrType = nullptr;

ElementCompiler::ElementCompiler(llvm::LLVMContext* ctx) :
  ctx_(ctx),
  builder_(*ctx) {
  ret_type_ = builder_.getVoidTy();
  ptr_type_ = builder_.getPtrTy();
  float_type_ = builder_.getFloatTy();

  new_node_func_type_ = llvm::FunctionType::get(ptr_type_, {}, false);
  set_width_func_type_ = GetYogaSetterFunctionType(float_type_);
  set_height_func_type_ = GetYogaSetterFunctionType(float_type_);
}

void ElementCompiler::InitYogaFunctions(llvm::Module* m) {
  new_node_func_ = m->getOrInsertFunction("YGNodeNew", new_node_func_type_);
#define INIT_YGFUNC(Name, Func) Name##_func_ = m->getOrInsertFunction(#Func, Name##_func_type_);
  FOR_EACH_YGFUNC(INIT_YGFUNC)
#undef INIT_YGFUNC
}

auto ElementCompiler::Compile() -> std::unique_ptr<llvm::Module> {
  auto m = std::make_unique<llvm::Module>("jit", *ctx_);
  InitYogaFunctions(m.get());

  llvm::StructType* DocumentType = llvm::StructType::create(*ctx_, "struct.dom::Document");
  DocumentType->setBody({
      ptr_type_,  // vtable
      ptr_type_,  // node_
      ptr_type_,  // props_
  });
  llvm::FunctionType* ConstructorType = nullptr;
  llvm::Function* Constructor = nullptr;
  llvm::BasicBlock* ConstructorEntry = nullptr;
  llvm::Value* ConstructorThisPtr = nullptr;
  CreateConstructor(m.get(), ptr_type_, {ptr_type_}, DocumentType, &ConstructorType, &Constructor, &ConstructorEntry,
                    &ConstructorThisPtr);

  auto new_node = CreateNewYGNode(new_node_func_, "yoga_node");
  SetWidth(new_node, 128.0f);
  SetHeight(new_node, 64.0f);
  builder_.CreateStore(new_node, ConstructorThisPtr);
  builder_.CreateRetVoid();
  return m;
}
}  // namespace kura::elem
