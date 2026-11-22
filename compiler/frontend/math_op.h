#ifndef KURA_MATH_OP_H
#define KURA_MATH_OP_H

#include <cstdint>
#include <iostream>

namespace kura {
#define FOR_EACH_MATH_OP(V) \
  V(Lerp)                   \
  V(Clamp)                  \
  V(Dot)                    \
  V(Length)                 \
  V(Normalize)

enum class MathOp : uint8_t {
  kLerp,       // lerp(a, b, t) -> Scalar | Unit | Vec2 | Vec3 | Rect | Color
  kClamp,      // clamp(x, min, max) -> Scalar | Unit | Vec2 | Vec3 | Rect | Color
  kDot,        // dot(a, b) -> Float (for Vec2, Vec3)
  kLength,     // length(v) -> Float (for Vec2, Vec3)
  kNormalize,  // normalize(v) -> Vec2 | Vec3
};

static inline auto Name(const MathOp& rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case MathOp::k##Name:       \
    return #Name;

    FOR_EACH_MATH_OP(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const MathOp& rhs) -> std::ostream& {
  return stream << Name(rhs);
}
}  // namespace kura

#endif  // KURA_MATH_OP_H
