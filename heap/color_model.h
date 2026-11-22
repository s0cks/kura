#ifndef KURA_COLOR_MODEL_H
#define KURA_COLOR_MODEL_H

#include <cstdint>
#include <iostream>
#include <string_view>

namespace kura {
#define FOR_EACH_COLOR_MODEL(V) \
  V(RgbaColor)                  \
  V(HslaColor)                  \
  V(HsbaColor)                  \
  V(SrgbaColor)

enum class ColorModel : uint8_t {
#define DEFINE_MODEL(Name) k##Name,
  FOR_EACH_COLOR_MODEL(DEFINE_MODEL)
#undef DEFINE_MODEL
};

static inline auto Name(const ColorModel rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case ColorModel::k##Name:   \
    return #Name;

    FOR_EACH_COLOR_MODEL(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const ColorModel& rhs) -> std::ostream& {
  return stream << Name(rhs);
}
}  // namespace kura

#endif  // KURA_COLOR_MODEL_H
