#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <stdlib.h>
#include <yoga/Yoga.h>

#include "backend/flow_graph.h"
#include "backend/flow_graph_builder.h"
#include "backend/ir.h"
#include "backend/ir_printer.h"
#include "common.h"
#include "element_compiler.h"
#include "frontend/expr_printer.h"
#include "frontend/parser.h"
#include "kura.h"
#include "module.h"

using namespace kura;

class ModulePrinter {
 private:
  std::ostream& stream_;
  Indent indent_{};

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto indent() const -> const Indent& {
    return indent_;
  }

  inline auto out() -> std::ostream& {
    return stream() << indent();
  }

 public:
  explicit ModulePrinter(std::ostream& stream, const Indent indent = 0) :
    stream_(stream),
    indent_(std::move(indent)) {}
  ~ModulePrinter() = default;

  auto PrintModule(Module* rhs) -> bool {
    if (!rhs)
      return true;

    out() << "Module: " << rhs->ToString() << std::endl;
    IndentScope indent_scope(indent_);
    const auto vis = [&](Function* func) {
      stream() << " - " << func->ToString() << std::endl;
      const auto flow_graph = FlowGraphBuilder::BuildFlowGraph(func->GetBody());
      if (!flow_graph)
        return false;

      IRPrinter::Print(std::cout, flow_graph, indent() + 1);
      return true;
    };

    return rhs->VisitFunctions(vis);
  }

  auto operator()(Module* rhs) -> bool {
    return PrintModule(rhs);
  }
};

auto main(int argc, char** argv) -> int {
  std::println("running kurac v{}", KURA_VERSION);
  if (argc < 2)
    return EXIT_FAILURE;

  Type::Init();

#ifdef KURA_DEBUG
  {
    TypePrinter printer(std::cout);
    if (!Type::VisitTypes(&printer))
      return EXIT_FAILURE;
  }

  {
    const std::function<VisitResult(Property*)> vis = [](Property* property) {
      std::cout << " - " << property->ToString() << std::endl;
      return true;
    };
    std::cout << "Properties:" << std::endl;
    if (!Property::VisitAllProperties(vis))
      return EXIT_FAILURE;
  }
#endif  // KURA_DEBUG

  LocalScope* scope = LocalScope::New();
  const std::string filename(argv[1]);

  Module* m = nullptr;
  Parser parser(scope);

  {
    const auto result = parser.ParseModuleFromFile(filename, &m);
    if (!result) {
      std::cerr << "parse error: " << result;
      return EXIT_FAILURE;
    }
  }

  ModulePrinter printer(std::cout);
  if (!printer(m))
    return EXIT_FAILURE;
  //
  // llvm::InitLLVM x(argc, argv);
  // llvm::InitializeNativeTarget();
  // llvm::InitializeNativeTargetAsmPrinter();
  // llvm::InitializeNativeTargetAsmParser();
  //
  // auto ctx = std::make_unique<llvm::LLVMContext>();
  // elem::ElementCompiler compiler(ctx.get());
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
  // auto ctor = SymExpect->toPtr<elem::Document* (*)(void)>();
  // const auto doc = ctor();
  // YGNodeCalculateLayout(doc->GetNode(), YGUndefined, YGUndefined, YGDirectionLTR);
  //
  // if (!elem::DOMPrinter::Print(doc))
  //   return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
