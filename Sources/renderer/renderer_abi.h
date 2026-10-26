#ifndef KURA_RENDERER_ABI_H
#define KURA_RENDERER_ABI_H

/*
 * The renderer ABI.
 *
 * This is the interface between the Kura runtime and a platform renderer
 * (OpenGL, Vulkan, DirectX, the headless renderer used by the tests, ...).
 * Every renderer implements `KuraRendererOps`; nothing outside this header is
 * required of it. It is plain C so that a renderer can be written in any
 * language, and so that the layout of everything here is the same on every
 * platform.
 *
 * Conventions
 *
 *  - The origin is the top-left corner of the surface, +x is right and +y is
 *    down, in pixels. Rects are (x, y, width, height).
 *  - Colors are non-premultiplied RGBA, each channel in [0, 1]. Channels
 *    outside that range are clamped.
 *  - Drawing is stateless: every call carries its own color. There is no
 *    "current color".
 *  - Draw calls are painted in the order they are made; a later call is on top
 *    of an earlier one.
 *  - All coordinates and color channels must be finite, and widths and heights
 *    must not be negative. A call that breaks that returns
 *    KURA_RENDERER_ERR_INVALID_ARG and has no effect.
 *  - Text is UTF-8, given as a pointer and a length in bytes (it need not be
 *    NUL terminated and may contain '\n').
 *  - A renderer is used from one thread at a time.
 *
 * Frames
 *
 * Everything except `measure_text` is only valid between `begin_frame` and
 * `end_frame`, and returns KURA_RENDERER_ERR_NOT_IN_FRAME outside of one.
 * `begin_frame` is given the size of the surface for that frame, so a host
 * that resizes needs nothing else.
 *
 * Clipping
 *
 * Clips form a stack. `push_clip` intersects the new rect with the current
 * clip, so clips nest (a scroll view inside a scroll view clips to both).
 * Every `push_clip` must be matched by a `pop_clip` before `end_frame`.
 * (This replaces the old set/reset pair, which could not nest.)
 *
 * Versioning
 *
 * KURA_RENDERER_ABI_VERSION changes whenever the meaning or layout of anything
 * here changes. Adding an op at the end of `KuraRendererOps` is compatible: a
 * host reads `struct_size` to find out which ops the renderer has.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KURA_RENDERER_ABI_VERSION 1u

typedef enum KuraRendererStatus {
  KURA_RENDERER_OK = 0,
  // A pointer was null, a number wasn't finite, a size was negative or too big.
  KURA_RENDERER_ERR_INVALID_ARG = 1,
  // The call needs a frame to be open, or `begin_frame` was called inside one.
  KURA_RENDERER_ERR_NOT_IN_FRAME = 2,
  KURA_RENDERER_ERR_IN_FRAME = 3,
  // `pop_clip` with nothing to pop.
  KURA_RENDERER_ERR_CLIP_UNDERFLOW = 4,
  // `end_frame` with clips still pushed. The frame is closed anyway.
  KURA_RENDERER_ERR_CLIP_UNBALANCED = 5,
  // The renderer ran out of memory or a device resource.
  KURA_RENDERER_ERR_OUT_OF_RESOURCES = 6,
} KuraRendererStatus;

typedef struct KuraVec2 {
  float x;
  float y;
} KuraVec2;

typedef struct KuraRect {
  float x;
  float y;
  float width;
  float height;
} KuraRect;

typedef struct KuraColor {
  float r;
  float g;
  float b;
  float a;
} KuraColor;

typedef struct KuraTextStyle {
  // The font size in pixels. Must be > 0.
  float size;
} KuraTextStyle;

typedef struct KuraTextMetrics {
  // The size of the text's bounding box: the widest line, and every line.
  float width;
  float height;
  // The distance from the top of the box to the baseline of the first line.
  float ascent;
} KuraTextMetrics;

// The largest surface a renderer has to support, in each dimension.
#define KURA_RENDERER_MAX_SURFACE_SIZE 16384u

typedef struct KuraRendererOps {
  // sizeof(KuraRendererOps) as the renderer was built. Set it, and a host
  // built against an older or newer header can still tell what is there.
  uint32_t struct_size;
  // KURA_RENDERER_ABI_VERSION as the renderer was built.
  uint32_t abi_version;

  // Destroys the renderer. `self` is not used again.
  void (*destroy)(void* self);

  KuraRendererStatus (*begin_frame)(void* self, uint32_t width, uint32_t height);
  KuraRendererStatus (*end_frame)(void* self);

  KuraRendererStatus (*push_clip)(void* self, KuraRect rect);
  KuraRendererStatus (*pop_clip)(void* self);

  // Fills the rect.
  KuraRendererStatus (*fill_rect)(void* self, KuraRect rect, KuraColor color);
  // Outlines the rect with a 1px line that lies inside it.
  KuraRendererStatus (*stroke_rect)(void* self, KuraRect rect, KuraColor color);
  // A 1px line from `from` to `to`.
  KuraRendererStatus (*draw_line)(void* self, KuraVec2 from, KuraVec2 to, KuraColor color);
  // Draws text with the top-left of its bounding box at `pos`.
  KuraRendererStatus (*draw_text)(void* self, KuraVec2 pos, const char* utf8, size_t length, KuraTextStyle style,
                                  KuraColor color);

  // Layout needs to know how big text will be before anything is drawn, so
  // this works outside of a frame. The same text and style always measure the
  // same.
  KuraRendererStatus (*measure_text)(void* self, const char* utf8, size_t length, KuraTextStyle style,
                                     KuraTextMetrics* out);
} KuraRendererOps;

// A renderer: its ops and the state they work on.
typedef struct KuraRenderer {
  const KuraRendererOps* ops;
  void* self;
} KuraRenderer;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // KURA_RENDERER_ABI_H
