#ifndef KURA_COLOR_STORAGE_H
#define KURA_COLOR_STORAGE_H

#include <algorithm>
#include <cmath>

#include "color_model.h"

namespace kura {
struct ColorStorage {
  float c0;
  float c1;
  float c2;
  float a;
};

inline auto SrgbToLinear(float c) -> float {
  if (c <= 0.04045f)
    return c / 12.92f;
  return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline auto LinearToSrgb(float c) -> float {
  if (c <= 0.0031308f)
    return c * 12.92f;
  return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline auto FoldColorConvert(const ColorStorage& src, const ColorModel from, const ColorModel to) -> ColorStorage {
  if (from == to)
    return src;

  // Fast direct Hsla <-> Hsba path (avoids RGB completely)
  if (from == ColorModel::kHslaColor && to == ColorModel::kHsbaColor) {
    const float bv = src.c2 + src.c1 * std::min(src.c2, 1.0f - src.c2);
    const float sv = (bv == 0.0f) ? 0.0f : 2.0f * (1.0f - src.c2 / bv);
    return {src.c0, sv, bv, src.a};
  }

  if (from == ColorModel::kHsbaColor && to == ColorModel::kHslaColor) {
    const float l = src.c2 * (1.0f - src.c1 * 0.5f);
    const float denom = std::min(l, 1.0f - l);
    const float sl = (denom == 0.0f) ? 0.0f : (src.c2 - l) / denom;
    return {src.c0, sl, l, src.a};
  }

  // Step 1: Convert `src` -> sRGB-encoded RgbaColor [0.0f, 1.0f]
  float r = src.c0;
  float g = src.c1;
  float b = src.c2;

  if (from == ColorModel::kSrgbaColor) {
    // Linear RGB -> sRGB
    r = LinearToSrgb(r);
    g = LinearToSrgb(g);
    b = LinearToSrgb(b);
  } else if (from == ColorModel::kHslaColor || from == ColorModel::kHsbaColor) {
    float c = 0.0f;
    float m = 0.0f;
    if (from == ColorModel::kHslaColor) {
      c = (1.0f - std::fabs(2.0f * src.c2 - 1.0f)) * src.c1;
      m = src.c2 - c * 0.5f;
    } else {
      c = src.c2 * src.c1;
      m = src.c2 - c;
    }

    float hp = std::fmod(src.c0, 360.0f) / 60.0f;
    if (hp < 0.0f)
      hp += 6.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));

    float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
    if (hp < 1.0f) {
      r1 = c;
      g1 = x;
    } else if (hp < 2.0f) {
      r1 = x;
      g1 = c;
    } else if (hp < 3.0f) {
      g1 = c;
      b1 = x;
    } else if (hp < 4.0f) {
      g1 = x;
      b1 = c;
    } else if (hp < 5.0f) {
      r1 = x;
      b1 = c;
    } else {
      r1 = c;
      b1 = x;
    }

    r = r1 + m;
    g = g1 + m;
    b = b1 + m;
  }

  // Step 2: Convert sRGB RgbaColor -> `to`
  if (to == ColorModel::kRgbaColor)
    return {r, g, b, src.a};

  if (to == ColorModel::kSrgbaColor)
    return {SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b), src.a};

  // Rgba -> Hsba -> Hsla
  const float max_c = std::max({r, g, b});
  const float min_c = std::min({r, g, b});
  const float delta = max_c - min_c;

  float h = 0.0f;
  if (delta != 0.0f) {
    if (max_c == r)
      h = std::fmod((g - b) / delta, 6.0f);
    else if (max_c == g)
      h = ((b - r) / delta) + 2.0f;
    else
      h = ((r - g) / delta) + 4.0f;
    h *= 60.0f;
    if (h < 0.0f)
      h += 360.0f;
  }

  const float s_hsba = (max_c == 0.0f) ? 0.0f : (delta / max_c);
  const ColorStorage hsba{h, s_hsba, max_c, src.a};
  if (to == ColorModel::kHsbaColor)
    return hsba;

  return FoldColorConvert(hsba, ColorModel::kHsbaColor, ColorModel::kHslaColor);
}
}  // namespace kura

#endif  // KURA_COLOR_STORAGE_H
