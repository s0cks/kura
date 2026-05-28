#include "ir.h"

namespace kura {
#define DEFINE_ACCEPT(Name)                                   \
  auto Name##Instr::Accept(InstructionVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                            \
  }
FOR_EACH_INSTRUCTION(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT
}  // namespace kura
