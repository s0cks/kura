#ifndef KURA_RENDERER_ABI_H
#define KURA_RENDERER_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KURA_RENDERER_ABI_VERSION 1u

typedef enum KuraRendererStatus {
  KURA_RENDERER_OK = 0,
  KURA_RENDERER_ERR_INVALID_ARG = 1,
  KURA_RENDERER_ERR_NOT_IN_FRAME = 2,
  KURA_RENDERER_ERR_IN_FRAME = 3,
  KURA_RENDERER_ERR_CLIP_UNDERFLOW = 4,
  KURA_RENDERER_ERR_CLIP_UNBALANCED = 5,
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
