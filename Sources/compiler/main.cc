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

#include "common.h"
#include "element_compiler.h"
#include "expr_printer.h"
#include "flow_graph.h"
#include "flow_graph_builder.h"
#include "ir.h"
#include "kura.h"
#include "module.h"
#include "parser.h"

using namespace kura;

static inline void PrintInstructions(EntryInstr* entry, Indent& indent);
static inline void PrintInstruction(Instruction* instr, Indent& indent) {
  if (instr->IsBranch()) {
    const auto branch = instr->AsBranch();
    std::cout << indent << " - " << instr->ToString() << std::endl;
    indent.Increment();

    std::cout << indent << " then: " << std::endl;
    indent.Increment();
    PrintInstructions(branch->GetThen(), indent);
    indent.Decrement();

    if (branch->HasElse()) {
      std::cout << indent << " else: " << std::endl;
      indent.Increment();
      PrintInstructions(branch->GetElse(), indent);
      indent.Decrement();
    }

    indent.Decrement();
    PrintInstructions(branch->GetJoin(), indent);
    return;
  }
  std::cout << indent << " - " << instr->ToString() << std::endl;
}

static inline void PrintInstructions(EntryInstr* entry, Indent& indent) {
  ForwardInstructionIterator iter(entry);
  while (iter.HasNext()) {
    const auto next = iter.Next();
    PrintInstruction(next, indent);
  }
}

auto main(int argc, char** argv) -> int {
  std::println("running kurac v{}", KURA_VERSION);
  if (argc < 2)
    return EXIT_FAILURE;

  LocalScope* scope = LocalScope::New();
  const std::string filename(argv[1]);

  Module* m = nullptr;
  Parser parser(scope);
  if (!parser.ParseModuleFromFile(filename, &m)) {
    std::cerr << "failed to parse Module from " << filename;
    return EXIT_FAILURE;
  }

  if (!m) {
    std::cerr << "failed to parse Module from " << filename;
    return EXIT_FAILURE;
  }

  Indent indent{};
  std::cout << "Module: " << m->name << std::endl;
  indent.Increment();
  const auto vis = [&](Function* func) {
    std::cout << indent << "- " << func->ToString() << ": " << std::endl;
    indent.Increment();

    FlowGraphBuilder builder{};
    const auto result = builder(func->body);
    const FlowGraph* flow_graph = builder.Build();
    std::cout << indent << "HIR: " << std::endl;
    PrintInstructions(flow_graph->GetEntry()->GetTarget(), indent);
    indent.Decrement();
    return true;
  };
  if (!m->VisitFunctions(vis)) {
    std::cerr << indent << "failed to visit functions" << std::endl;
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
