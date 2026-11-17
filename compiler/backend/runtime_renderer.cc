// The renderer side of the runtime ABI: the draw calls compiled code makes, forwarded through the
// renderer ABI (renderer/renderer_abi.h) to whichever renderer the host set on this thread.
//
// This file depends on nothing but the ABI headers and `kura_rt_panic`, so it can be tested without
// the rest of the compiler.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "backend/runtime_abi.h"

namespace {
// The renderer is thread-local so that two threads (two UIs, or a UI and a test) can render at the
// same time without sharing a renderer. It's a copy of the handle: the host still owns the renderer.
thread_local KuraRenderer tl_renderer{};
thread_local KuraRendererStatus tl_status = KURA_RENDERER_OK;
thread_local const char* tl_op = "";
thread_local uint32_t tl_viewport_width = 1024;
thread_local uint32_t tl_viewport_height = 1024;

auto StatusName(const KuraRendererStatus status) -> const char* {
  switch (status) {
    case KURA_RENDERER_OK:
      return "OK";
    case KURA_RENDERER_ERR_INVALID_ARG:
      return "an invalid argument";
    case KURA_RENDERER_ERR_NOT_IN_FRAME:
      return "a draw call made outside of a frame";
    case KURA_RENDERER_ERR_IN_FRAME:
      return "a frame that was begun inside another frame";
    case KURA_RENDERER_ERR_CLIP_UNDERFLOW:
      return "a clip that was popped without being pushed";
    case KURA_RENDERER_ERR_CLIP_UNBALANCED:
      return "a frame that ended with clips still pushed";
    case KURA_RENDERER_ERR_OUT_OF_RESOURCES:
      return "the renderer running out of resources";
  }
  return "an unknown error";
}

auto Current(const char* op) -> const KuraRendererOps& {
  if (tl_renderer.ops == nullptr) {
    const auto message =
        std::string(op) + ": there is no renderer on this thread, the host has to call kura_rt_set_renderer first";
    kura_rt_panic(message.c_str());
  }
  return *tl_renderer.ops;
}

inline void Record(const char* op, const KuraRendererStatus status) {
  tl_op = op;
  tl_status = status;
}

// Coordinates are the bits of an f64, the same as a Float in a KuraValue.
inline auto Coordinate(const uint64_t bits) -> float {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return static_cast<float>(value);
}

inline auto UnpackColor(const uint64_t packed) -> KuraColor {
  const auto channel = [&](const int shift) {
    return static_cast<float>((packed >> shift) & 0xFF) / 255.0f;
  };
  return KuraColor{channel(24), channel(16), channel(8), channel(0)};
}
}  // namespace

namespace {
// A box that is `w` by `h` with its corners rounded by `radius`, a row of pixels at a time, with `inset` measured from
// the middle of the row: how far in from the left (and right) the shape starts. `top` is the distance of the row's
// middle from the nearest of the top and the bottom.
inline auto RoundedInset(const float radius, const float row_middle, const float height) -> float {
  const auto edge = std::min(row_middle, height - row_middle);
  if (radius <= 0 || edge >= radius)
    return 0;
  const auto dy = radius - edge;
  return radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
}

struct RowSpans {
  // the spans in a row, left to right, relative to the box: one for a fill, up to two for a border's sides
  std::array<std::pair<float, float>, 2> spans{};
  int count = 0;

  auto operator==(const RowSpans& rhs) const -> bool {
    if (count != rhs.count)
      return false;
    for (int i = 0; i < count; i++) {
      if (spans[i] != rhs.spans[i])
        return false;
    }
    return true;
  }
};

// Fills what `rows_of` says is in each row, and joins the rows that say the same into one rectangle.
template <typename RowsOf>
void FillRows(const KuraRendererOps& ops, void* self, const KuraRect& box, const KuraColor& color, RowsOf rows_of) {
  const auto rows = static_cast<int>(std::ceil(box.height));
  int start = 0;
  RowSpans run{};
  const auto flush = [&](const int end) {
    if (end <= start)
      return;
    const auto top = box.y + static_cast<float>(start);
    const auto height = std::min(static_cast<float>(end - start), box.height - static_cast<float>(start));
    for (int i = 0; i < run.count; i++) {
      const KuraRect rect{box.x + run.spans[i].first, top, run.spans[i].second - run.spans[i].first, height};
      if (rect.width > 0 && rect.height > 0)
        Record("kura_rt_paint_box", ops.fill_rect(self, rect, color));
    }
  };
  for (int row = 0; row < rows; row++) {
    const auto row_height = std::min(1.0f, box.height - static_cast<float>(row));
    const auto spans = rows_of(static_cast<float>(row) + row_height / 2);
    if (row == 0 || !(spans == run)) {
      flush(row);
      start = row;
      run = spans;
    }
  }
  flush(rows);
}

inline auto Scaled(KuraColor color, const float opacity) -> KuraColor {
  color.a *= std::clamp(opacity, 0.0f, 1.0f);
  return color;
}
}  // namespace

extern "C" {
void kura_rt_paint_box(const KuraRect* rect, const uint64_t fill, const uint64_t border_color,
                       const uint64_t border_width_bits, const uint64_t radius_bits, const uint64_t opacity_bits) {
  const auto& ops = Current("kura_rt_paint_box");
  const auto box = *rect;
  if (box.width <= 0 || box.height <= 0)
    return;

  // what is rounded, bordered or faded can't be more than half the box
  const auto half = std::min(box.width, box.height) / 2;
  const auto radius = std::clamp(static_cast<float>(Coordinate(radius_bits)), 0.0f, half);
  const auto border = std::clamp(static_cast<float>(Coordinate(border_width_bits)), 0.0f, half);
  const auto opacity = static_cast<float>(Coordinate(opacity_bits));
  const auto fill_color = Scaled(UnpackColor(fill), opacity);
  const auto border_rgba = Scaled(UnpackColor(border_color), opacity);

  // the shape, rounded by `radius`
  if (fill_color.a > 0) {
    if (radius <= 0) {
      Record("kura_rt_paint_box", ops.fill_rect(tl_renderer.self, box, fill_color));
    } else {
      FillRows(ops, tl_renderer.self, box, fill_color, [&](const float middle) {
        RowSpans row{};
        const auto inset = RoundedInset(radius, middle, box.height);
        row.spans[0] = {inset, box.width - inset};
        row.count = 1;
        return row;
      });
    }
  }

  // the ring between the shape and the same shape `border` smaller, which is rounded by that much less
  if (border > 0 && border_rgba.a > 0) {
    const auto inner_radius = std::max(0.0f, radius - border);
    const auto inner_height = box.height - 2 * border;
    const auto inner_width = box.width - 2 * border;
    FillRows(ops, tl_renderer.self, box, border_rgba, [&](const float middle) {
      RowSpans row{};
      const auto outer = RoundedInset(radius, middle, box.height);
      const auto inside = middle > border && middle < box.height - border && inner_width > 0 && inner_height > 0;
      if (!inside) {
        row.spans[0] = {outer, box.width - outer};
        row.count = 1;
        return row;
      }

      const auto inner = border + RoundedInset(inner_radius, middle - border, inner_height);
      if (inner > outer) {
        row.spans[row.count++] = {outer, inner};
        row.spans[row.count++] = {box.width - inner, box.width - outer};
      }
      return row;
    });
  }
}

KuraRendererStatus kura_rt_set_renderer(const KuraRenderer* renderer) {
  if (renderer == nullptr) {
    tl_renderer = KuraRenderer{};
    Record("", KURA_RENDERER_OK);
    return KURA_RENDERER_OK;
  }

  const auto ops = renderer->ops;
  if (ops == nullptr || ops->abi_version != KURA_RENDERER_ABI_VERSION || ops->struct_size < sizeof(KuraRendererOps))
    return KURA_RENDERER_ERR_INVALID_ARG;

  tl_renderer = *renderer;
  Record("", KURA_RENDERER_OK);
  return KURA_RENDERER_OK;
}

const KuraRenderer* kura_rt_get_renderer() {
  return tl_renderer.ops != nullptr ? &tl_renderer : nullptr;
}

void kura_rt_begin_frame(const uint32_t width, const uint32_t height) {
  // what the last frame's view said its events meant is not what this one's will
  kura_rt_events_clear();
  const auto& ops = Current("kura_rt_begin_frame");
  Record("kura_rt_begin_frame", ops.begin_frame(tl_renderer.self, width, height));
}

void kura_rt_end_frame() {
  const auto& ops = Current("kura_rt_end_frame");
  // over everything that was drawn
  kura_rt_tooltip_draw();
  Record("kura_rt_end_frame", ops.end_frame(tl_renderer.self));
}

void kura_rt_fill_rect(const KuraRect* rect, const uint64_t color) {
  const auto& ops = Current("kura_rt_fill_rect");
  Record("kura_rt_fill_rect", ops.fill_rect(tl_renderer.self, *rect, UnpackColor(color)));
}

void kura_rt_draw_line(uint64_t x1bits, uint64_t y1bits, uint64_t x2bits, uint64_t y2bits, uint64_t color) {
  const auto& ops = Current("kura_rt_draw_line");
  const KuraVec2 start{Coordinate(x1bits), Coordinate(y1bits)};
  const KuraVec2 end{Coordinate(x2bits), Coordinate(y2bits)};
  Record("kura_rt_draw_line", ops.draw_line(tl_renderer.self, start, end, UnpackColor(color)));
}

void kura_rt_push_clip(const KuraRect* rect) {
  const auto& ops = Current("kura_rt_push_clip");
  Record("kura_rt_push_clip", ops.push_clip(tl_renderer.self, *rect));
  kura_rt_event_clip_push(rect->x, rect->y, rect->width, rect->height);
}

void kura_rt_pop_clip() {
  const auto& ops = Current("kura_rt_pop_clip");
  Record("kura_rt_pop_clip", ops.pop_clip(tl_renderer.self));
  kura_rt_event_clip_pop();
}

void kura_rt_set_viewport(const uint32_t width, const uint32_t height) {
  tl_viewport_width = width;
  tl_viewport_height = height;
}

uint32_t kura_rt_viewport_width() {
  return tl_viewport_width;
}

uint32_t kura_rt_viewport_height() {
  return tl_viewport_height;
}

void kura_rt_render_text(const double x, const double y, const char* utf8, const uint64_t length, const float size,
                         const char* family, const uint64_t family_length, const uint64_t color) {
  const auto& ops = Current("kura_rt_draw_text");
  const KuraVec2 pos{static_cast<float>(x), static_cast<float>(y)};
  const KuraTextStyle style{size, family, family == nullptr ? 0 : static_cast<size_t>(family_length)};
  Record("kura_rt_draw_text",
         ops.draw_text(tl_renderer.self, pos, utf8, static_cast<size_t>(length), style, UnpackColor(color)));
}

int kura_rt_measure_text(const char* utf8, const uint64_t length, const float size, const char* family,
                         const uint64_t family_length, float* width, float* height) {
  if (tl_renderer.ops == nullptr || tl_renderer.ops->measure_text == nullptr)
    return 1;
  KuraTextMetrics metrics{};
  const KuraTextStyle style{size, family, family == nullptr ? 0 : static_cast<size_t>(family_length)};
  if (tl_renderer.ops->measure_text(tl_renderer.self, utf8, static_cast<size_t>(length), style, &metrics) !=
      KURA_RENDERER_OK)
    return 2;
  *width = metrics.width;
  *height = metrics.height;
  return 0;
}

void kura_rt_check_renderer() {
  if (tl_status == KURA_RENDERER_OK)
    return;
  const auto message = std::string("the renderer rejected ") + tl_op + ": " + StatusName(tl_status);
  tl_status = KURA_RENDERER_OK;
  kura_rt_panic(message.c_str());
}
}
