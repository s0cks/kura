#ifndef KURA_BINARY_OP_H
#define KURA_BINARY_OP_H

#include <ostream>
#include <string_view>

namespace kura {
#define FOR_EACH_BINARY_OP(V) \
  V(Add)                      \
  V(Subtract)                 \
  V(Multiply)                 \
  V(Divide)                   \
  V(Modulus)                  \
  V(Eq)                       \
  V(Neq)                      \
  V(GreaterThan)              \
  V(GreaterThanEqual)         \
  V(LessThan)                 \
  V(LessThanEqual)            \
  V(BinaryAnd)                \
  V(BinaryOr)                 \
  V(Pipe)

enum BinaryOp {
#define DEFINE_BINARY_OP(Name) k##Name,
  FOR_EACH_BINARY_OP(DEFINE_BINARY_OP)
#undef DEFINE_BINARY_OP
};

static inline auto ToString(const BinaryOp rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case BinaryOp::k##Name:     \
    return #Name;
    FOR_EACH_BINARY_OP(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const BinaryOp rhs) -> std::ostream& {
  return stream << ToString(rhs);
}
}  // namespace kura

#endif  // KURA_BINARY_OP_H
