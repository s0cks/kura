#ifndef KURA_RENDERER_HEADLESS_H
#define KURA_RENDERER_HEADLESS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/renderer_abi.h"

// The headless renderer: an implementation of the renderer ABI (see
// renderer_abi.h) that draws nowhere a person can see. It exists so the rest of
// Kura can be tested without a window, a GPU or a driver, and so there is one
// small renderer that shows what the ABI is supposed to mean.
//
// It does two things with what it is asked to draw:
//
//  - It records every accepted call as a `Command`, in order. Tests assert on
//    those, or dump them (`DumpCommands`) and compare against a golden file.
//  - It rasterizes into an RGBA8 framebuffer in software, with clipping and
//    alpha blending, so a test can also assert on pixels (`PixelAt`) or look
//    at a frame (`ToPpm`). This can be turned off.
//
// Both are deterministic: the same calls always produce the same commands and
// the same bytes, on any machine. Nothing here depends on a font, a locale or
// floating point contraction.
//
// Rasterization rules (these are the headless renderer's, not part of the ABI:
// a GPU renderer will differ at the edges of shapes):
//
//  - A pixel belongs to a rect if the pixel's center is inside it, and the
//    rect includes its left and top edges but not its right and bottom ones. So
//    a rect at (0, 0) with size 10x10 covers pixels 0..9 in both directions,
//    (0.5, 0.5, 10, 10) covers the same ones (pixel 0's center is on its edge),
//    and (0.6, 0.6, 10, 10) covers pixels 1..10.
//  - `stroke_rect` draws the outermost ring of the pixels `fill_rect` would
//    cover, once per pixel (the corners are not blended twice).
//  - `draw_line` is Bresenham between the pixels that contain the two
//    endpoints (a point (x, y) is in pixel (floor(x), floor(y))). Both end
//    pixels are drawn.
//  - Blending is source-over on non-premultiplied 8-bit channels, in integer
//    arithmetic, with the color quantized with round(c * 255).
//  - Text is measured and recorded, but its glyphs are not rasterized: there
//    is no font here to rasterize them with.
//
// Text metrics are a fixed model, so layout can be tested exactly: every code
// point is `0.5 * size` wide, a line is `1.25 * size` tall, and the baseline is
// `size` below the top of a line. Text is split into lines at '\n', and the
// width is that of the widest line. (So "" is one line tall, and "a\n" is two.)
namespace kura::renderer {
class HeadlessImpl;

// One accepted call, recorded.
struct Command {
  enum class Kind { kFillRect, kStrokeRect, kLine, kText, kPushClip, kPopClip };

  Kind kind = Kind::kFillRect;
  // kFillRect, kStrokeRect and kPushClip.
  KuraRect rect{};
  // kLine: its start. kText: the position of its top-left corner.
  KuraVec2 from{};
  // kLine: its end.
  KuraVec2 to{};
  KuraColor color{};
  // kText.
  std::string text{};
  float font_size = 0.0f;

  auto operator==(const Command& rhs) const -> bool;
  auto operator!=(const Command& rhs) const -> bool {
    return !(*this == rhs);
  }
};

struct Rgba8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 0;

  auto operator==(const Rgba8& rhs) const -> bool {
    return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
  }
  auto operator!=(const Rgba8& rhs) const -> bool {
    return !(*this == rhs);
  }
};

struct HeadlessOptions {
  // Draw into a framebuffer as well as recording commands.
  bool rasterize = true;
  // What each frame is cleared to.
  KuraColor clear = {0.0f, 0.0f, 0.0f, 0.0f};
};

class HeadlessRenderer {
 public:
  explicit HeadlessRenderer(const HeadlessOptions& options = {});
  ~HeadlessRenderer();
  HeadlessRenderer(const HeadlessRenderer&) = delete;
  HeadlessRenderer& operator=(const HeadlessRenderer&) = delete;

  // This renderer through the ABI. The handle borrows the renderer: its
  // `destroy` does nothing, so the handle must not outlive `this`.
  auto Abi() -> KuraRenderer;

  auto InFrame() const -> bool;
  // The size of the surface set by the last `begin_frame`.
  auto GetWidth() const -> uint32_t;
  auto GetHeight() const -> uint32_t;
  // How many frames have been ended.
  auto GetFrameCount() const -> uint64_t;
  // How many clips are pushed right now.
  auto GetClipDepth() const -> size_t;

  // The calls of the current frame, or of the last one if none is open. The
  // next `begin_frame` clears them.
  auto GetCommands() const -> const std::vector<Command>&;
  // One line per command, e.g. `fill-rect 0 0 10 10 #ff0000ff`, preceded by a
  // `frame <width>x<height>` line. Numbers are printed in their shortest form that
  // reads back the same (`10`, `0.5`), whatever the locale, and `-0` is `0`.
  auto DumpCommands() const -> std::string;

  // The framebuffer: width * height pixels, row-major from the top-left, four
  // bytes (r, g, b, a) each. Empty when rasterizing is off.
  auto GetPixels() const -> const std::vector<uint8_t>&;
  // The pixel at (x, y), or transparent black when it's out of bounds.
  auto PixelAt(uint32_t x, uint32_t y) const -> Rgba8;
  // How many pixels are exactly `color`.
  auto CountPixels(const Rgba8& color) const -> uint64_t;
  // The framebuffer as a binary PPM (P6), composited over `background`.
  auto ToPpm(const Rgba8& background = {255, 255, 255, 255}) const -> std::string;

  // Measures text the way `measure_text` does.
  static auto MeasureText(const std::string& text, float font_size) -> KuraTextMetrics;

 private:
  std::unique_ptr<HeadlessImpl> impl_;
};
}  // namespace kura::renderer

extern "C" {
typedef struct KuraHeadlessOptions {
  // Non-zero to rasterize.
  uint8_t rasterize;
  KuraColor clear;
} KuraHeadlessOptions;

// Creates a headless renderer that owns itself: `renderer->ops->destroy` frees
// it. `options` may be null for the defaults (rasterize, clear to transparent).
KuraRendererStatus kura_headless_renderer_create(const KuraHeadlessOptions* options, KuraRenderer* renderer);
}

#endif  // KURA_RENDERER_HEADLESS_H
