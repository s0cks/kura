// The renderer side of the runtime ABI: the draw calls compiled code makes, forwarded through the
// renderer ABI (renderer/renderer_abi.h) to whichever renderer the host set on this thread.
//
// This file depends on nothing but the ABI headers and `kura_rt_panic`, so it can be tested without
// the rest of the compiler.
#include <cstdint>
#include <cstring>
#include <string>

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
  const auto channel = [&](const int shift) { return static_cast<float>((packed >> shift) & 0xFF) / 255.0f; };
  return KuraColor{channel(24), channel(16), channel(8), channel(0)};
}
}  // namespace

extern "C" {
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
  Record("kura_rt_end_frame", ops.end_frame(tl_renderer.self));
}

void kura_rt_fill_rect(const uint64_t xbits, const uint64_t ybits, const uint64_t wbits, const uint64_t hbits,
                       const uint64_t color) {
  const auto& ops = Current("kura_rt_fill_rect");
  const KuraRect rect{Coordinate(xbits), Coordinate(ybits), Coordinate(wbits), Coordinate(hbits)};
  Record("kura_rt_fill_rect", ops.fill_rect(tl_renderer.self, rect, UnpackColor(color)));
}

void kura_rt_push_clip(const uint64_t xbits, const uint64_t ybits, const uint64_t wbits, const uint64_t hbits) {
  const auto& ops = Current("kura_rt_push_clip");
  const KuraRect rect{Coordinate(xbits), Coordinate(ybits), Coordinate(wbits), Coordinate(hbits)};
  Record("kura_rt_push_clip", ops.push_clip(tl_renderer.self, rect));
  kura_rt_event_clip_push(rect.x, rect.y, rect.width, rect.height);
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
  Record("kura_rt_draw_text", ops.draw_text(tl_renderer.self, pos, utf8, static_cast<size_t>(length), style,
                                            UnpackColor(color)));
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
