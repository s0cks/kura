#ifndef KURA_GEOMETRY_H
#define KURA_GEOMETRY_H

// The basic geometry that the compiler, the runtime and the renderers share: a point or a size in the plane, a point
// in space, and a rectangle. They are plain values of 32-bit floats, laid out exactly like the renderer ABI's
// KuraVec2, KuraVec3 and KuraRect (abi_check.cc keeps them in step), so one is handed to a renderer, or read from the
// layout, as it is, with no conversion.
//
// This header has no dependencies, so that anything can include it.
#include <algorithm>
#include <cmath>
#include <string>

namespace kura::geom {
struct Vec2 {
  float x = 0;
  float y = 0;

  constexpr Vec2() = default;
  constexpr Vec2(const float x_, const float y_) :
    x(x_),
    y(y_) {}

  constexpr auto operator==(const Vec2& rhs) const -> bool {
    return x == rhs.x && y == rhs.y;
  }

  constexpr auto operator+(const Vec2& rhs) const -> Vec2 {
    return {x + rhs.x, y + rhs.y};
  }

  constexpr auto operator-(const Vec2& rhs) const -> Vec2 {
    return {x - rhs.x, y - rhs.y};
  }

  constexpr auto operator*(const float rhs) const -> Vec2 {
    return {x * rhs, y * rhs};
  }

  constexpr auto Dot(const Vec2& rhs) const -> float {
    return x * rhs.x + y * rhs.y;
  }

  inline auto Length() const -> float {
    return std::sqrt(Dot(*this));
  }

  inline auto ToString() const -> std::string {
    return "Vec2(x=" + std::to_string(x) + ", y=" + std::to_string(y) + ")";
  }
};

struct Vec3 {
  float x = 0;
  float y = 0;
  float z = 0;

  constexpr Vec3() = default;
  constexpr Vec3(const float x_, const float y_, const float z_) :
    x(x_),
    y(y_),
    z(z_) {}

  constexpr auto operator==(const Vec3& rhs) const -> bool {
    return x == rhs.x && y == rhs.y && z == rhs.z;
  }

  constexpr auto operator+(const Vec3& rhs) const -> Vec3 {
    return {x + rhs.x, y + rhs.y, z + rhs.z};
  }

  constexpr auto operator-(const Vec3& rhs) const -> Vec3 {
    return {x - rhs.x, y - rhs.y, z - rhs.z};
  }

  constexpr auto operator*(const float rhs) const -> Vec3 {
    return {x * rhs, y * rhs, z * rhs};
  }

  constexpr auto Dot(const Vec3& rhs) const -> float {
    return x * rhs.x + y * rhs.y + z * rhs.z;
  }

  constexpr auto Cross(const Vec3& rhs) const -> Vec3 {
    return {y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x};
  }

  inline auto Length() const -> float {
    return std::sqrt(Dot(*this));
  }

  inline auto ToString() const -> std::string {
    return "Vec3(x=" + std::to_string(x) + ", y=" + std::to_string(y) + ", z=" + std::to_string(z) + ")";
  }
};

// A rectangle by its top left corner and its size. Y grows downward, as on the screen. A rectangle with no width or
// no height has no area, and holds no point.
struct Rect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  constexpr Rect() = default;
  constexpr Rect(const float x_, const float y_, const float width_, const float height_) :
    x(x_),
    y(y_),
    width(width_),
    height(height_) {}
  constexpr Rect(const Vec2& origin, const Vec2& size) :
    x(origin.x),
    y(origin.y),
    width(size.x),
    height(size.y) {}

  constexpr auto operator==(const Rect& rhs) const -> bool {
    return x == rhs.x && y == rhs.y && width == rhs.width && height == rhs.height;
  }

  constexpr auto Origin() const -> Vec2 {
    return {x, y};
  }

  constexpr auto Size() const -> Vec2 {
    return {width, height};
  }

  constexpr auto Right() const -> float {
    return x + width;
  }

  constexpr auto Bottom() const -> float {
    return y + height;
  }

  constexpr auto Center() const -> Vec2 {
    return {x + width / 2, y + height / 2};
  }

  constexpr auto IsEmpty() const -> bool {
    return !(width > 0 && height > 0);
  }

  // The left and top edges are in the rectangle, and the right and bottom ones are not, so that rectangles that
  // share an edge don't share a point.
  constexpr auto Contains(const Vec2& point) const -> bool {
    return point.x >= x && point.x < Right() && point.y >= y && point.y < Bottom();
  }

  // What is in both, or an empty rectangle at the corner that they would have met in.
  constexpr auto Intersect(const Rect& rhs) const -> Rect {
    const auto left = std::max(x, rhs.x);
    const auto top = std::max(y, rhs.y);
    const auto right = std::min(Right(), rhs.Right());
    const auto bottom = std::min(Bottom(), rhs.Bottom());
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
  }

  // The rectangle with its edges moved in by `amount` on every side; out for a negative one. It never has a negative
  // size: a rectangle that is inset more than it is large is empty, at its middle.
  constexpr auto Inset(const float amount) const -> Rect {
    const auto w = std::max(0.0f, width - 2 * amount);
    const auto h = std::max(0.0f, height - 2 * amount);
    return {width - 2 * amount < 0 ? x + width / 2 : x + amount, height - 2 * amount < 0 ? y + height / 2 : y + amount,
            w, h};
  }

  inline auto ToString() const -> std::string {
    return "Rect(x=" + std::to_string(x) + ", y=" + std::to_string(y) + ", width=" + std::to_string(width) +
           ", height=" + std::to_string(height) + ")";
  }
};
}  // namespace kura::geom

#endif  // KURA_GEOMETRY_H
