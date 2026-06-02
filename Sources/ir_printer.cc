#include "ir_printer.h"

#include "ir.h"

namespace kura {
void IRPrinter::PrintInstruction(Instruction* instr) {
  out() << instr->ToString() << std::endl;
  if (instr->IsBranch()) {
    const auto branch = instr->AsBranch();
    {
      out() << "then: " << std::endl;
      IndentScope then_scope(indent_);
      PrintInstructions(branch->GetThen());
    }

    if (branch->HasElse()) {
      out() << "else: " << std::endl;
      IndentScope else_scope(indent_);
      PrintInstructions(branch->GetElse());
    }

    return PrintInstructions(branch->GetJoin());
  }
}

void IRPrinter::PrintInstructions(EntryInstr* entry) {
  if (!entry)
    return;

  ForwardInstructionIterator iter(entry);
  while (iter.HasNext()) {
    const auto next = iter.Next();
    PrintInstruction(next);
  }
}
}  // namespace kura
