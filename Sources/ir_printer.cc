#include "ir_printer.h"

#include "ir.h"

namespace kura {
void IRPrinter::PrintInstruction(Instruction* instr) {
  out() << instr->ToString() << std::endl;
  if (instr->IsBranch()) {
    const auto branch = instr->AsBranch();
    {
      out() << "then: " << std::endl;
      IndentScope indent_scope(indent_);
      PrintInstructions(branch->GetThen());
    }

    if (branch->HasElse()) {
      out() << "else: " << std::endl;
      IndentScope indent_scope(indent_);
      PrintInstructions(branch->GetElse());
    }

    PrintInstructions(branch->GetJoin());
    return;
  }
}

void IRPrinter::PrintInstructions(EntryInstr* entry) {
  ForwardInstructionIterator iter(entry);
  while (iter.HasNext()) {
    const auto next = iter.Next();
    PrintInstruction(next);
  }
}
}  // namespace kura
