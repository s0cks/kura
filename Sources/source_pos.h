#ifndef KURA_SOURCE_POS_H
#define KURA_SOURCE_POS_H

#include <cstdint>

namespace kura {
struct SourcePos {
  uint64_t row;
  uint64_t col;
};
} // namespace kura

#endif // KURA_SOURCE_POS_H
