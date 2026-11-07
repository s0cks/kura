#include "backend/ir_printer.h"

#include "backend/compile_type.h"
#include "backend/ir.h"
#include "common.h"
#include "function.h"
#include "local_scope.h"

namespace kura {
void IRPrinter::PrintInstruction(Instruction* instr) {
  out();
  if (instr->IsDefinition() && !instr->IsReturn())
    stream() << "%" << instr->GetId() << " = ";

  stream() << instr->ToString();
  if (instr->IsDefinition() && !instr->IsReturn() && instr->IsDefinition()) {
    const auto type = static_cast<Definition*>(instr)->GetCompileType();
    if (type)
      stream() << " : " << type->ToString();
  }

  stream() << std::endl;
}

void IRPrinter::PrintBlock(EntryInstr* block) {
  out() << "B" << block->GetBlockId();
  if (block->HasLabel())
    stream() << " " << block->GetLabel()->GetData();

  stream() << ":";
  if (const auto n = block->GetNumberOfPredecessors(); n > 0 && !block->GetPredecessorAt(0)->IsGraphEntry()) {
    stream() << "  ; preds:";
    for (auto idx = 0; idx < n; idx++) {
      if (!block->GetPredecessorAt(idx)->IsGraphEntry())
        stream() << " B" << block->GetPredecessorAt(idx)->GetBlockId();
    }
  }

  stream() << std::endl;

  indent_.Increment();
  for (auto instr = block->GetNext(); instr != nullptr; instr = instr->GetNext())
    PrintInstruction(instr);

  indent_.Decrement();
}

void IRPrinter::PrintGraph(FlowGraph* graph) {
  out() << "fn " << graph->GetName() << "(";
  const auto& types = graph->GetParameterTypes();
  for (auto idx = 0; idx < graph->GetNumberOfParameters(); idx++) {
    stream() << (idx ? ", " : "");
    if (idx < types.size())
      stream() << types[idx]->ToString();
    else
      stream() << "?";
  }

  stream() << ")";
  if (graph->GetReturnType())
    stream() << " => " << graph->GetReturnType()->ToString();

  stream() << ":" << std::endl;
  indent_.Increment();
  for (const auto block : graph->GetReversePostorder()) {
    if (block->IsGraphEntry())
      continue;

    PrintBlock(block);
  }

  indent_.Decrement();
}

void IRPrinter::PrintModule(ModuleGraph* module) {
  for (const auto graph : module->GetGraphs()) {
    if (graph->IsExtern()) {
      stream() << "extern fn " << graph->GetName() << std::endl << std::endl;
      continue;
    }

    PrintGraph(graph);
    stream() << std::endl;
  }
}
}  // namespace kura
