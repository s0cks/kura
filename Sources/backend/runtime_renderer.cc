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

void kura_rt_check_renderer() {
  if (tl_status == KURA_RENDERER_OK)
    return;
  const auto message = std::string("the renderer rejected ") + tl_op + ": " + StatusName(tl_status);
  tl_status = KURA_RENDERER_OK;
  kura_rt_panic(message.c_str());
}
}
