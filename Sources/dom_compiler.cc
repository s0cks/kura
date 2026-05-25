#include "dom_compiler.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <yoga/Yoga.h>

namespace kura::dom {
static llvm::StructType* BoxClassType = nullptr;
static llvm::PointerType* BoxPtrType = nullptr;

DOMCompiler::DOMCompiler(llvm::LLVMContext* ctx) :
  ctx_(ctx),
  builder_(*ctx) {
  ret_type_ = builder_.getVoidTy();
  ptr_type_ = builder_.getPtrTy();
  float_type_ = builder_.getFloatTy();

  new_node_func_type_ = llvm::FunctionType::get(ptr_type_, {}, false);
  set_width_func_type_ = llvm::FunctionType::get(builder_.getVoidTy(),
                                                 {
                                                     ptr_type_,
                                                     float_type_,
                                                 },
                                                 false);
  set_height_func_type_ = llvm::FunctionType::get(builder_.getVoidTy(),
                                                  {
                                                      ptr_type_,
                                                      float_type_,
                                                  },
                                                  false);
}

DOMCompiler::~DOMCompiler() {}

void DOMCompiler::CompileWidthProperty(llvm::Value* node_ref, WidthProperty* prop) {}

void DOMCompiler::CompileHeightProperty(llvm::Value* node_ref, HeightProperty* prop) {}

auto DOMCompiler::Compile() -> std::unique_ptr<llvm::Module> {
  auto m = std::make_unique<llvm::Module>("jit", *ctx_);
  const auto new_node_func = m->getOrInsertFunction("YGNodeNew", new_node_func_type_);
  const auto set_width_func = m->getOrInsertFunction("YGNodeStyleSetWidth", set_width_func_type_);
  const auto set_height_func = m->getOrInsertFunction("YGNodeStyleSetHeight", set_height_func_type_);

  std::vector<llvm::Type*> ParamTypes = {
      ptr_type_,
  };
  llvm::FunctionType* ConstructorType = llvm::FunctionType::get(ret_type_, ParamTypes, false);
  llvm::Function* ConstructorFunc =
      llvm::Function::Create(ConstructorType, llvm::Function::ExternalLinkage, "Test_Constructor", m.get());

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(*ctx_, "entry", ConstructorFunc);
  builder_.SetInsertPoint(entry);

  llvm::StructType* DocumentType = llvm::StructType::create(*ctx_, "struct.dom::Document");
  DocumentType->setBody({
      ptr_type_,  // vtable
      ptr_type_,  // node_
      ptr_type_,  // props_
  });

  llvm::Value* ThisPtr = ConstructorFunc->getArg(0);
  ThisPtr->setName("this");
  llvm::Value* NodeFieldPtr = builder_.CreateStructGEP(DocumentType, ThisPtr, 1, "node_ptr");
  auto new_node = CreateNewYGNode(new_node_func, "yoga_node");
  SetValueWithSetter(set_width_func, new_node, 128.0);
  SetValueWithSetter(set_height_func, new_node, 64.0);
  builder_.CreateStore(new_node, NodeFieldPtr);
  builder_.CreateRetVoid();
  return m;
}

void DOMCompiler::Init() {}
}  // namespace kura::dom
