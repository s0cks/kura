#ifndef KURA_ASSERT_OP_H
#define KURA_ASSERT_OP_H

#include <cstdint>

namespace kura {
#define FOR_EACH_ASSERT_OP(V) \
  V(Eq)                       \
  V(Ne)                       \
  V(Lt)                       \
  V(Lte)                      \
  V(Gt)                       \
  V(Gte)

enum class AssertOp : uint8_t {
  kAssertDefault = 0,
#define DEFINE_OP(Name) kAssert##Name,
  FOR_EACH_ASSERT_OP(DEFINE_OP)
#undef DEFINE_OP
};

}  // namespace kura

#endif  // KURA_ASSERT_OP_H
