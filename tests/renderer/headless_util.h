#ifndef KURA_TESTS_HEADLESS_UTIL_H
#define KURA_TESTS_HEADLESS_UTIL_H

#include <map>
#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include "abi_print.h"
#include "renderer/renderer_headless.h"

namespace kura::renderer {
inline auto operator<<(std::ostream& out, const Rgba8& value) -> std::ostream& {
  return out << "rgba(" << static_cast<int>(value.r) << ", " << static_cast<int>(value.g) << ", "
             << static_cast<int>(value.b) << ", " << static_cast<int>(value.a) << ")";
}
}  // namespace kura::renderer

namespace kura::test {
constexpr KuraColor kRed = {1.0f, 0.0f, 0.0f, 1.0f};
constexpr KuraColor kGreen = {0.0f, 1.0f, 0.0f, 1.0f};
constexpr KuraColor kBlue = {0.0f, 0.0f, 1.0f, 1.0f};
constexpr KuraColor kWhite = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr KuraColor kYellow = {1.0f, 1.0f, 0.0f, 1.0f};

constexpr renderer::Rgba8 kRedPixel = {255, 0, 0, 255};
constexpr renderer::Rgba8 kGreenPixel = {0, 255, 0, 255};
constexpr renderer::Rgba8 kBluePixel = {0, 0, 255, 255};
constexpr renderer::Rgba8 kWhitePixel = {255, 255, 255, 255};
constexpr renderer::Rgba8 kYellowPixel = {255, 255, 0, 255};
constexpr renderer::Rgba8 kClearPixel = {0, 0, 0, 0};

// Draws through the ABI, like the runtime will, and expects every call to work.
// (To look at a status, call the op directly: `ops()`.)
class Drawer {
 public:
  explicit Drawer(renderer::HeadlessRenderer& renderer) :
    abi_(renderer.Abi()) {}

  auto Ops() const -> const KuraRendererOps& {
    return *abi_.ops;
  }

  auto Self() const -> void* {
    return abi_.self;
  }

  void Begin(const uint32_t width, const uint32_t height) {
    EXPECT_EQ(Ops().begin_frame(Self(), width, height), KURA_RENDERER_OK);
  }

  void End() {
    EXPECT_EQ(Ops().end_frame(Self()), KURA_RENDERER_OK);
  }

  void Fill(const KuraRect& rect, const KuraColor& color) {
    EXPECT_EQ(Ops().fill_rect(Self(), rect, color), KURA_RENDERER_OK);
  }

  void Stroke(const KuraRect& rect, const KuraColor& color) {
    EXPECT_EQ(Ops().stroke_rect(Self(), rect, color), KURA_RENDERER_OK);
  }

  void Line(const KuraVec2& from, const KuraVec2& to, const KuraColor& color) {
    EXPECT_EQ(Ops().draw_line(Self(), from, to, color), KURA_RENDERER_OK);
  }

  void Text(const KuraVec2& pos, const std::string& text, const float size, const KuraColor& color) {
    EXPECT_EQ(Ops().draw_text(Self(), pos, text.data(), text.size(), KuraTextStyle{size}, color),
                   KURA_RENDERER_OK);
  }

  void Push(const KuraRect& rect) {
    EXPECT_EQ(Ops().push_clip(Self(), rect), KURA_RENDERER_OK);
  }

  void Pop() {
    EXPECT_EQ(Ops().pop_clip(Self()), KURA_RENDERER_OK);
  }

 private:
  KuraRenderer abi_;
};

// The framebuffer as text, one character per pixel and one line per row, so a
// picture of a frame can be read in a diff. Transparent pixels are '.', and
// every other color is the character `palette` gives it ('?' if it isn't
// there).
inline auto AsciiArt(const renderer::HeadlessRenderer& renderer, const std::map<char, renderer::Rgba8>& palette)
    -> std::string {
  std::string out;
  for (uint32_t y = 0; y < renderer.GetHeight(); y++) {
    for (uint32_t x = 0; x < renderer.GetWidth(); x++) {
      const auto pixel = renderer.PixelAt(x, y);
      char symbol = pixel.a == 0 ? '.' : '?';
      for (const auto& [key, color] : palette) {
        if (color == pixel)
          symbol = key;
      }
      out.push_back(symbol);
    }
    out.push_back('\n');
  }
  return out;
}
}  // namespace kura::test

#endif  // KURA_TESTS_HEADLESS_UTIL_H
