#ifndef KURA_UNARY_OP_H
#define KURA_UNARY_OP_H

#include <ostream>
#include <string_view>

namespace kura {
#define FOR_EACH_UNARY_OP(V) \
  V(Plus)                    \
  V(Minus)                   \
  V(Bang)

enum UnaryOp {
#define DEFINE_OP(Name) k##Name,
  FOR_EACH_UNARY_OP(DEFINE_OP)
#undef DEFINE_OP
};

static inline constexpr auto ToString(const UnaryOp rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case UnaryOp::k##Name:      \
    return #Name;
    FOR_EACH_UNARY_OP(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const UnaryOp& rhs) -> std::ostream& {
  return stream << ToString(rhs);
}
}  // namespace kura

#endif  // KURA_UNARY_OP_H
