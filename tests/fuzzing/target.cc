#include <cstddef>
#include <cstdint>

#include "log.h"

extern "C" int LLVMFuzzerTestOne(const uint8_t* data, size_t size) {
  if (size < 4)
    return 0;
  // TODO(@s0cks): parse test
  return 0;
}
