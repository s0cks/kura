#ifndef KURA_SWIZZLE_H
#define KURA_SWIZZLE_H

#include <optional>
#include <string_view>
#include <vector>

#include "backend/compile_type.h"

namespace kura {
struct SwizzleMask {
  std::vector<int> indices{};  // 2, 3, or 4 lane indices
  bool is_color = false;
};

inline auto ParseSwizzle(CompileType* ty, std::string_view prop) -> std::optional<SwizzleMask> {
  if (prop.size() < 2 || prop.size() > 4)
    return std::nullopt;

  auto lane_of = [&](char ch) -> std::optional<int> {
    if (ty->IsVec2()) {
      if (ch == 'x')
        return 0;
      if (ch == 'y')
        return 1;
      return std::nullopt;
    }
    if (ty->IsVec3()) {
      if (ch == 'x')
        return 0;
      if (ch == 'y')
        return 1;
      if (ch == 'z')
        return 2;
      return std::nullopt;
    }
    if (ty->IsRect()) {
      if (ch == 'x')
        return 0;
      if (ch == 'y')
        return 1;
      if (ch == 'w')
        return 2;
      if (ch == 'h')
        return 3;
      return std::nullopt;
    }
    if (ty->IsRgbaColor() || ty->IsSrgbaColor()) {
      if (ch == 'r')
        return 0;
      if (ch == 'g')
        return 1;
      if (ch == 'b')
        return 2;
      if (ch == 'a')
        return 3;
      return std::nullopt;
    }
    if (ty->IsHslaColor()) {
      if (ch == 'h')
        return 0;
      if (ch == 's')
        return 1;
      if (ch == 'l')
        return 2;
      if (ch == 'a')
        return 3;
      return std::nullopt;
    }
    if (ty->IsHsbaColor()) {
      if (ch == 'h')
        return 0;
      if (ch == 's')
        return 1;
      if (ch == 'v' || ch == 'b')
        return 2;
      if (ch == 'a')
        return 3;
      return std::nullopt;
    }
    return std::nullopt;
  };

  SwizzleMask mask{};
  mask.is_color = ty->IsColor();
  for (char ch : prop) {
    const auto idx = lane_of(ch);
    if (!idx)
      return std::nullopt;
    mask.indices.push_back(*idx);
  }
  return mask;
}
}  // namespace kura

#endif  // KURA_SWIZZLE_H
