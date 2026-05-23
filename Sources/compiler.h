#ifndef KURA_COMPILER_H
#define KURA_COMPILER_H

#include "common.h"

namespace kura {
class Compiler {
  DEFINE_NON_COPYABLE_TYPE(Compiler);

 public:
  Compiler() = default;
  ~Compiler() = default;
};
}  // namespace kura

#endif  // KURA_COMPILER_H
