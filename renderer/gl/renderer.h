#ifndef KURA_RENDERER_GL_H
#define KURA_RENDERER_GL_H

// The OpenGL renderer: a KuraRenderer (see renderer_abi.h) that draws with OpenGL 3.3 core.
//
// It doesn't create a window or a context. The host does, makes the context
// current on the calling thread, and then creates the renderer. Every call
// through the ops table (and `destroy`) must be made on a thread where that
// context is current. The renderer loads GL through libepoxy, so the host
// doesn't need to hand it a loader.
//
// A frame draws into whatever framebuffer is bound when `begin_frame` is
// called (the window's, or an FBO the host bound), with (0, 0) at the top-left
// and one unit per pixel. The renderer saves the GL state it changes in
// `begin_frame` and restores it in `end_frame`, so it can draw an overlay in
// the middle of a host's own rendering. Between the two the host shouldn't
// touch GL state that affects drawing.
//
// What's drawn today: fill_rect, stroke_rect, draw_line, push_clip and
// pop_clip, and draw_text. Text is shaped with HarfBuzz and drawn from a glyph
// atlas the renderer keeps for each font: the `family` of a text style is looked
// up on the system (a comma-separated list is tried in order, and a family
// that isn't installed is drawn in the system's default font), and `measure_text`
// measures with the same shaping, so what layout measures is what is drawn.

#include <stdint.h>

#include "renderer/renderer_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KuraGlOptions {
  // sizeof(KuraGlOptions). Must be set.
  uint32_t struct_size;
  // Nonzero: `begin_frame` clears the color buffer to `clear` first. Zero: the
  // frame is drawn over whatever is already there (an overlay on a game).
  uint32_t clear_on_begin;
  KuraColor clear;
} KuraGlOptions;

// Creates an OpenGL renderer that owns itself: `renderer->ops->destroy` frees
// it. `options` may be NULL, which clears to opaque black on every frame.
//
// Returns KURA_RENDERER_ERR_INVALID_ARG if `renderer` is NULL, `options` is
// malformed, or there is no OpenGL 3.3+ context current on this thread, and
// KURA_RENDERER_ERR_OUT_OF_RESOURCES if the shaders or buffers can't be made.
KuraRendererStatus kura_gl_renderer_create(const KuraGlOptions* options, KuraRenderer* renderer);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // KURA_RENDERER_GL_H
