#include "backend/stub.h"

namespace kura {
#define STUB_FN(Name) void Name##Stub::Emit(llvm::IRBuilder<>& builder)

STUB_FN(TracyBeginZone) {}

STUB_FN(TracyEndZone) {}
}  // namespace kura
