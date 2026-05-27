#include <cstdio>
#include <cstdlib>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <stdlib.h>
#include <yoga/Yoga.h>

// clang-format off
#include <antlr4-runtime.h>
// clang-format on

#include "KuraLexer.h"
#include "KuraParser.h"
#include "dom.h"
#include "dom_compiler.h"
#include "expr_builder.h"

using namespace kura;

auto main(int argc, char** argv) -> int {
  std::string source{};
  {
    FILE* file = fopen(argv[1], "r");
    if (!file)
      return EXIT_FAILURE;
    fseek(file, 0, SEEK_END);
    const auto size = ftell(file);
    source.resize(size);
    rewind(file);
    fread(source.data(), sizeof(char), size, file);
    fclose(file);
  }

  std::cout << "source:" << std::endl << source << std::endl;

  antlr4::ANTLRInputStream stream(source);

  KuraLexer lexer(&stream);
  antlr4::CommonTokenStream tokens(&lexer);
  KuraParser parser(&tokens);
  auto* tree = parser.source();

  std::cout << "--- Parse Tree ---" << std::endl;
  std::cout << tree->toStringTree(&parser) << std::endl;
  std::cout << std::endl;

  expr::ExprBuilder expr_builder{};
  const auto m = std::any_cast<Module*>(expr_builder.visit(tree));
  if (!m)
    return EXIT_FAILURE;
  std::cout << "Module: " << m->GetName() << std::endl;
  const auto vis = [](Function* func) {
    std::cout << " - " << func->GetName() << std::endl;
    return true;
  };
  if (!m->VisitFunctions(vis)) {
    std::cerr << "failed to visit functions" << std::endl;
    return EXIT_FAILURE;
  }

  // llvm::InitLLVM x(argc, argv);
  // llvm::InitializeNativeTarget();
  // llvm::InitializeNativeTargetAsmPrinter();
  // llvm::InitializeNativeTargetAsmParser();
  //
  // dom::Document* doc = new dom::Document();
  // doc->AddProperty(new dom::WidthProperty(1024));
  // doc->AddProperty(new dom::HeightProperty(680));
  //
  // auto ctx = std::make_unique<llvm::LLVMContext>();
  // dom::DOMCompiler compiler(ctx.get());
  //
  // auto m = compiler.Compile();
  //
  // auto JITExpect = llvm::orc::LLJITBuilder().create();
  // if (!JITExpect) {
  //   llvm::errs() << "failed to create LLJIT: " << JITExpect.takeError() << "\n";
  //   return EXIT_FAILURE;
  // }
  // auto JIT = std::move(*JITExpect);
  //
  // llvm::orc::ThreadSafeModule TSM(std::move(m), std::move(ctx));
  // if (auto err = JIT->addIRModule(std::move(TSM))) {
  //   llvm::errs() << "failed to add module to JIT: " << std::move(err) << "\n";
  //   return EXIT_FAILURE;
  // }
  //
  // auto SymExpect = JIT->lookup("Test_Constructor");
  // if (!SymExpect) {
  //   llvm::errs() << "function lookup failed: " << SymExpect.takeError() << "\n";
  //   return EXIT_FAILURE;
  // }
  //
  // auto ctor = SymExpect->toPtr<void (*)(dom::Document*)>();
  // ctor(doc);
  // YGNodeCalculateLayout(doc->node(), YGUndefined, YGUndefined, YGDirectionLTR);
  //
  // if (!dom::DOMPrinter::Print(doc))
  //   return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
