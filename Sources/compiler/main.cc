#include <cstdlib>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <stdlib.h>
#include <yoga/Yoga.h>

#include "dom.h"
#include "dom_compiler.h"

using namespace kura;

auto main(int argc, char** argv) -> int {
  llvm::InitLLVM x(argc, argv);
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  dom::Document* doc = new dom::Document();
  doc->AddProperty(new dom::WidthProperty(1024));
  doc->AddProperty(new dom::HeightProperty(680));

  auto ctx = std::make_unique<llvm::LLVMContext>();
  dom::DOMCompiler compiler(ctx.get());

  auto m = compiler.Compile();

  auto JITExpect = llvm::orc::LLJITBuilder().create();
  if (!JITExpect) {
    llvm::errs() << "failed to create LLJIT: " << JITExpect.takeError() << "\n";
    return EXIT_FAILURE;
  }
  auto JIT = std::move(*JITExpect);

  llvm::orc::ThreadSafeModule TSM(std::move(m), std::move(ctx));
  if (auto err = JIT->addIRModule(std::move(TSM))) {
    llvm::errs() << "failed to add module to JIT: " << std::move(err) << "\n";
    return EXIT_FAILURE;
  }

  auto SymExpect = JIT->lookup("Test_Constructor");
  if (!SymExpect) {
    llvm::errs() << "function lookup failed: " << SymExpect.takeError() << "\n";
    return EXIT_FAILURE;
  }

  auto ctor = SymExpect->toPtr<void (*)(dom::Document*)>();
  ctor(doc);
  YGNodeCalculateLayout(doc->node(), YGUndefined, YGUndefined, YGDirectionLTR);

  if (!dom::DOMPrinter::Print(doc))
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
