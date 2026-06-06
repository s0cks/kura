#include "ir_printer.h"

#include "common.h"
#include "ir.h"

namespace kura {
void IRPrinter::PrintInstruction(Instruction* instr) {
  if (instr->IsLabel()) {
    indent_.Decrement();
    line();
    const auto label = instr->AsLabel()->GetLabel();
    out() << label->GetData() << ": " << std::endl;
    indent_.Increment();
    return;
  }

  out() << instr->ToString() << std::endl;
  if (instr->IsBranch()) {
    const auto branch = instr->AsBranch();
    PushBlock(branch->GetThen());
    if (branch->HasElse())
      PushBlock(branch->GetElse());
    PushBlock(branch->GetJoin());
  } else if (instr->IsGoto()) {
    PushBlock(instr->AsGoto()->GetTarget());
  }
}

void IRPrinter::PrintInstructions(EntryInstr* entry) {
  if (!entry)
    return;

  PushBlock(entry);
  do {
    const auto next_block = GetNextBlock();
    if (!next_block)
      return;

#ifdef KURA_DEBUG

    if (next_block->HasLabel()) {
      line();
      out() << next_block->GetLabel()->GetData() << ": " << std::endl;
    }

#endif  // KURA_DEBUG

    {
      indent_.Increment();
      ForwardInstructionIterator iter(next_block);
      while (iter.HasNext()) {
        const auto next = iter.Next();
        PrintInstruction(next);
      }
      indent_.Decrement();
    }
  } while (true);
}
}  // namespace kura
