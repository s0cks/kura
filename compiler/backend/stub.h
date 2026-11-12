#ifndef KURA_COMPILER_STUB_H
#define KURA_COMPILER_STUB_H

#include <llvm/IR/IRBuilder.h>

namespace kura {
class Stub {};

#define DECLARE_STUB(Name)                        \
  struct Name##Stub : public Stub {               \
    static void Emit(llvm::IRBuilder<>& builder); \
  };

DECLARE_STUB(TracyBeginZone);
DECLARE_STUB(TracyEndZone);
}  // namespace kura

#endif  // KURA_COMPILER_STUB_H
