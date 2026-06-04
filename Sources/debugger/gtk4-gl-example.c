
// gtk4_gl_example.c  —  buffered, indexed GTK4 + OpenGL renderer
//
// Key changes from v1:
//   • Per-vertex colour: colour is stored in each vertex, so any number of
//     draw calls with different colours can be batched without a GPU flush.
//   • Two persistent batches: one for GL_TRIANGLES (fills), one for GL_LINES
//     (outlines/lines). Each uses an EBO so shared corners are indexed rather
//     than duplicated.
//   • Flushing is deferred: vertices accumulate in CPU arrays and are sent to
//     the GPU in a single glDrawElements call at render_end() — or earlier
//     only when the clip state changes or a buffer is about to overflow.
//
// Build:
//   gcc $(pkg-config --cflags --libs gtk4 epoxy) \
//       -w -o gtk4_gl_example gtk4_gl_example.c

#include <epoxy/egl.h>
#include <gtk/gtk.h>
#include <stddef.h>  // offsetof
#include <string.h>  // memcpy

// ---------------------------------------------------------------------------
// Shaders — per-vertex colour, pixel-space coords (origin top-left)
// ---------------------------------------------------------------------------

static const char* kVertSrc =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec4 a_color;\n"
    "uniform vec2 u_res;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "  vec2 p = a_pos / u_res * 2.0 - 1.0;\n"
    "  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "  v_color = a_color;\n"
    "}\n";

static const char* kFragSrc =
    "#version 330 core\n"
    "in  vec4 v_color;\n"
    "out vec4 out_color;\n"
    "void main() { out_color = v_color; }\n";

// ---------------------------------------------------------------------------
// Vertex layout
// ---------------------------------------------------------------------------

typedef struct {
  float x, y;        // pixel-space position
  float r, g, b, a;  // colour
} Vertex;

// ---------------------------------------------------------------------------
// Batch — owns one VAO/VBO/EBO and accumulates draws of one primitive mode
// ---------------------------------------------------------------------------

#define MAX_VERTS   16384
#define MAX_INDICES 32768

typedef struct {
  Vertex verts[MAX_VERTS];
  GLuint indices[MAX_INDICES];
  int vert_count;
  int index_count;
  GLuint vao, vbo, ebo;
  GLenum mode;  // GL_TRIANGLES or GL_LINES
} Batch;

static void batch_init(Batch* b, GLenum mode) {
  b->mode = mode;
  b->vert_count = 0;
  b->index_count = 0;

  glGenVertexArrays(1, &b->vao);
  glGenBuffers(1, &b->vbo);
  glGenBuffers(1, &b->ebo);

  glBindVertexArray(b->vao);

  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(b->verts), NULL, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(b->indices), NULL, GL_DYNAMIC_DRAW);

  // attrib 0: position (xy)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
  // attrib 1: colour (rgba)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

  glBindVertexArray(0);
}

static void batch_free(Batch* b) {
  glDeleteVertexArrays(1, &b->vao);
  glDeleteBuffers(1, &b->vbo);
  glDeleteBuffers(1, &b->ebo);
}

// Upload accumulated data and issue one indexed draw call, then reset.
static void batch_flush(Batch* b) {
  if (b->index_count == 0)
    return;

  glBindVertexArray(b->vao);

  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, b->vert_count * sizeof(Vertex), b->verts);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, b->index_count * sizeof(GLuint), b->indices);

  glDrawElements(b->mode, b->index_count, GL_UNSIGNED_INT, 0);

  glBindVertexArray(0);

  b->vert_count = 0;
  b->index_count = 0;
}

// Append nv vertices and ni local indices (0-based within this call) to the
// batch. Indices are automatically offset by the current vertex base.
// Triggers an early flush if the buffers would overflow.
static void batch_add(Batch* b, const Vertex* verts, int nv, const GLuint* local_inds, int ni) {
  if (b->vert_count + nv > MAX_VERTS || b->index_count + ni > MAX_INDICES) {
    batch_flush(b);
  }
  GLuint base = (GLuint)b->vert_count;
  for (int i = 0; i < ni; i++) {
    b->indices[b->index_count + i] = local_inds[i] + base;
  }
  memcpy(&b->verts[b->vert_count], verts, (size_t)nv * sizeof(Vertex));
  b->vert_count += nv;
  b->index_count += ni;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

typedef struct {
  GLuint prog;
  GLint loc_res;
  Batch tris;   // filled shapes  (GL_TRIANGLES)
  Batch lines;  // outlines/lines (GL_LINES)
  int width;
  int height;
  float color[4];
} Renderer;

static Renderer g_ren;

static void renderer_init(void) {
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &kVertSrc, NULL);
  glCompileShader(vs);

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &kFragSrc, NULL);
  glCompileShader(fs);

  g_ren.prog = glCreateProgram();
  glAttachShader(g_ren.prog, vs);
  glAttachShader(g_ren.prog, fs);
  glLinkProgram(g_ren.prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  g_ren.loc_res = glGetUniformLocation(g_ren.prog, "u_res");

  batch_init(&g_ren.tris, GL_TRIANGLES);
  batch_init(&g_ren.lines, GL_LINES);
}

static void renderer_free(void) {
  glDeleteProgram(g_ren.prog);
  batch_free(&g_ren.tris);
  batch_free(&g_ren.lines);
}

// Flush both batches to the GPU.
static void renderer_flush(void) {
  glUseProgram(g_ren.prog);
  batch_flush(&g_ren.tris);
  batch_flush(&g_ren.lines);
}

// ---------------------------------------------------------------------------
// Public draw API
// ---------------------------------------------------------------------------

// Call once at the start of every frame.
void render_begin(int width, int height) {
  g_ren.width = width;
  g_ren.height = height;
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_ren.prog);
  glUniform2f(g_ren.loc_res, (float)width, (float)height);
}

// Call once at the end of every frame — flushes all buffered geometry.
void render_end(void) {
  renderer_flush();
  glUseProgram(0);
  glDisable(GL_SCISSOR_TEST);
}

// Set the colour used by subsequent draw commands (RGBA, 0‥1).
void render_set_color(float r, float g, float b, float a) {
  g_ren.color[0] = r;
  g_ren.color[1] = g;
  g_ren.color[2] = b;
  g_ren.color[3] = a;
}

// Queue a solid filled rectangle.
// Generates 4 vertices, 6 indices (2 triangles sharing a diagonal edge).
void render_fill_rect(float x, float y, float w, float h) {
  float r = g_ren.color[0], g = g_ren.color[1], b = g_ren.color[2], a = g_ren.color[3];

  const Vertex verts[4] = {
      {x, y, r, g, b, a},          // 0 top-left
      {x + w, y, r, g, b, a},      // 1 top-right
      {x + w, y + h, r, g, b, a},  // 2 bottom-right
      {x, y + h, r, g, b, a},      // 3 bottom-left
  };
  const GLuint inds[6] = {0, 1, 3, 1, 2, 3};  // CCW winding
  batch_add(&g_ren.tris, verts, 4, inds, 6);
}

// Queue a hollow rectangle outline.
// Generates 4 vertices, 8 indices (4 GL_LINES edge pairs).
void render_draw_rect(float x, float y, float w, float h) {
  float r = g_ren.color[0], g = g_ren.color[1], b = g_ren.color[2], a = g_ren.color[3];

  const Vertex verts[4] = {
      {x, y, r, g, b, a},          // 0 top-left
      {x + w, y, r, g, b, a},      // 1 top-right
      {x + w, y + h, r, g, b, a},  // 2 bottom-right
      {x, y + h, r, g, b, a},      // 3 bottom-left
  };
  // Each edge is an independent line segment (GL_LINES pairs)
  const GLuint inds[8] = {0, 1, 1, 2, 2, 3, 3, 0};
  batch_add(&g_ren.lines, verts, 4, inds, 8);
}

// Queue a line segment.
// Generates 2 vertices, 2 indices.
void render_draw_line(float x0, float y0, float x1, float y1) {
  float r = g_ren.color[0], g = g_ren.color[1], b = g_ren.color[2], a = g_ren.color[3];

  const Vertex verts[2] = {
      {x0, y0, r, g, b, a},
      {x1, y1, r, g, b, a},
  };
  const GLuint inds[2] = {0, 1};
  batch_add(&g_ren.lines, verts, 2, inds, 2);
}

// Set a scissor clip rectangle.
// FLUSHES buffered geometry first — GL scissor is per-draw-call state,
// so any pending vertices must be submitted before the state changes.
void render_clip_rect(float x, float y, float w, float h) {
  renderer_flush();
  int gl_y = g_ren.height - (int)(y + h);  // flip: top-left → GL bottom-left
  glEnable(GL_SCISSOR_TEST);
  glScissor((int)x, gl_y, (int)w, (int)h);
}

// Remove the clip rectangle. Also flushes pending geometry.
void render_clip_reset(void) {
  renderer_flush();
  glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------
// GTK4 GtkGLArea signal handlers
// ---------------------------------------------------------------------------

static void on_realize(GtkGLArea* area, gpointer data) {
  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area) != NULL)
    return;
  renderer_init();
}

static void on_unrealize(GtkGLArea* area, gpointer data) {
  gtk_gl_area_make_current(area);
  renderer_free();
}

static gboolean on_render(GtkGLArea* area, GdkGLContext* ctx, gpointer data) {
  int w = gtk_widget_get_width(GTK_WIDGET(area));
  int h = gtk_widget_get_height(GTK_WIDGET(area));

  render_begin(w, h);

  // ------------------------------------------------------------------
  // 1. Dark panel — no flush yet, just queued
  // ------------------------------------------------------------------
  render_set_color(0.18f, 0.22f, 0.32f, 1.0f);
  render_fill_rect(10, 10, w - 20, h - 20);

  // ------------------------------------------------------------------
  // 2. Clip — flushes the panel above, then sets scissor
  // ------------------------------------------------------------------
  float cx = 30, cy = 30, cw = w - 60, ch = h - 60;
  render_clip_rect(cx, cy, cw, ch);

  // ------------------------------------------------------------------
  // 3. Filled rects with different colours — all batched together,
  //    zero GPU state changes between them.
  // ------------------------------------------------------------------
  render_set_color(0.25f, 0.80f, 0.45f, 1.0f);
  render_fill_rect(cx - 20, cy - 20, 180, 130);  // hangs outside clip

  render_set_color(0.95f, 0.55f, 0.10f, 1.0f);
  render_fill_rect(w / 2.0f - 70, h / 2.0f - 45, 140, 90);

  render_set_color(0.40f, 0.55f, 0.95f, 1.0f);
  render_fill_rect(40, h - 130, 110, 70);

  // ------------------------------------------------------------------
  // 4. Outlined rects — batched in the lines batch alongside the
  //    lines below. No flush between outline calls.
  // ------------------------------------------------------------------
  render_set_color(1.0f, 1.0f, 1.0f, 0.85f);
  render_draw_rect(w / 2.0f - 70, h / 2.0f - 45, 140, 90);

  render_set_color(0.95f, 0.25f, 0.30f, 1.0f);
  render_draw_rect(w - 90, 50, 120, 80);

  render_set_color(0.80f, 0.80f, 0.20f, 1.0f);
  render_draw_rect(40, h - 130, 110, 70);

  // ------------------------------------------------------------------
  // 5. Grid of horizontal lines — all batched in the same lines batch
  // ------------------------------------------------------------------
  for (int i = 0; i < 6; i++) {
    float t = (float)i / 5.0f;
    float ly = h - 170 + i * 14;
    render_set_color(t, 0.6f, 1.0f - t, 0.75f);
    render_draw_line(50, ly, w - 50, ly);
  }

  // ------------------------------------------------------------------
  // 6. Reset clip (flushes the tri + line batches above), then draw
  //    the border and clip-region indicator unclipped.
  // ------------------------------------------------------------------
  render_clip_reset();

  render_set_color(1.0f, 1.0f, 1.0f, 0.30f);
  render_draw_rect(10, 10, w - 20, h - 20);

  render_set_color(0.90f, 0.85f, 0.20f, 0.50f);
  render_draw_rect(cx, cy, cw, ch);

  // render_end() does the final glDrawElements for these last two rects
  render_end();
  return TRUE;
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

static void on_activate(GtkApplication* app, gpointer data) {
  GtkWidget* win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "GTK4 + OpenGL — Buffered Indexed Renderer");
  gtk_window_set_default_size(GTK_WINDOW(win), 720, 520);

  GtkWidget* gl = gtk_gl_area_new();
  gtk_gl_area_set_required_version(GTK_GL_AREA(gl), 3, 3);
  g_signal_connect(gl, "realize", G_CALLBACK(on_realize), NULL);
  g_signal_connect(gl, "unrealize", G_CALLBACK(on_unrealize), NULL);
  g_signal_connect(gl, "render", G_CALLBACK(on_render), NULL);

  gtk_window_set_child(GTK_WINDOW(win), gl);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char* argv[]) {
  GtkApplication* app = gtk_application_new("com.example.gl_renderer", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
