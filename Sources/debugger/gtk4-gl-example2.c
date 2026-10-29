// gtk4_gl_example.c — GTK4+OpenGL buffered renderer
//
// Features:
//   Geometry:    fill/draw rect, circle, ellipse, rounded-rect, polygon
//   Paths:       move/line/cubic/quad/arc, fill (ear-clip), stroke (miter-join)
//   Bezier:      Bezier/QuadBez types, eval/tangent/normal/split/length
//                render_stroke_bezier, render_fill_bezier_area, bezier_ease
//   Transforms:  push/pop state, translate, rotate, scale
//   Text:        HarfBuzz shaping, FreeType rasterisation, atlas cache
//   Ticker:      POSIX clock_gettime timer, GTK-independent; GTK drives it
//                via add_tick_callback but the time source is platform-only
//
// Build:
//   gcc $(pkg-config --cflags --libs gtk4 epoxy harfbuzz freetype2) \
//       -lm -w -o gtk4_gl_example gtk4_gl_example.c

#include <epoxy/gl.h>
#include <ft2build.h>
#include <gtk/gtk.h>
#include <hb-ft.h>
#include <hb.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>  // clock_gettime — no GTK dependency

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

static const char* kGeomVert =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "uniform vec2 u_res;\n"
    "out vec4 v_col;\n"
    "void main(){\n"
    "  vec2 p=a_pos/u_res*2.0-1.0;\n"
    "  gl_Position=vec4(p.x,-p.y,0,1);\n"
    "  v_col=a_col;\n"
    "}\n";

static const char* kGeomFrag =
    "#version 330 core\n"
    "in vec4 v_col; out vec4 o;\n"
    "void main(){ o=v_col; }\n";

static const char* kTextVert =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_col;\n"
    "uniform vec2 u_res;\n"
    "out vec2 v_uv; out vec4 v_col;\n"
    "void main(){\n"
    "  vec2 p=a_pos/u_res*2.0-1.0;\n"
    "  gl_Position=vec4(p.x,-p.y,0,1);\n"
    "  v_uv=a_uv; v_col=a_col;\n"
    "}\n";

static const char* kTextFrag =
    "#version 330 core\n"
    "in vec2 v_uv; in vec4 v_col;\n"
    "uniform sampler2D u_atlas;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  float a=texture(u_atlas,v_uv).r;\n"
    "  o=vec4(v_col.rgb, v_col.a*a);\n"
    "}\n";

// ---------------------------------------------------------------------------
// 2D affine transform
//   | a  c  tx |
//   | b  d  ty |
//   | 0  0   1 |
// ---------------------------------------------------------------------------

typedef struct {
  float a, b, c, d, tx, ty;
} Mat2;

static Mat2 mat2_id(void) {
  return (Mat2){1, 0, 0, 1, 0, 0};
}

static void mat2_pt(Mat2 m, float ix, float iy, float* ox, float* oy) {
  *ox = m.a * ix + m.c * iy + m.tx;
  *oy = m.b * ix + m.d * iy + m.ty;
}

// Right-multiply: m = m * T(dx,dy)
static void mat2_translate(Mat2* m, float dx, float dy) {
  m->tx += m->a * dx + m->c * dy;
  m->ty += m->b * dx + m->d * dy;
}

// Right-multiply: m = m * R(angle)
static void mat2_rotate(Mat2* m, float a) {
  float cs = cosf(a), sn = sinf(a);
  float na = m->a * cs + m->c * sn, nc = -m->a * sn + m->c * cs;
  float nb = m->b * cs + m->d * sn, nd = -m->b * sn + m->d * cs;
  m->a = na;
  m->c = nc;
  m->b = nb;
  m->d = nd;
}

// Right-multiply: m = m * S(sx,sy)
static void mat2_scale(Mat2* m, float sx, float sy) {
  m->a *= sx;
  m->b *= sx;
  m->c *= sy;
  m->d *= sy;
}

// ---------------------------------------------------------------------------
// Render state (saved/restored by push/pop)
// ---------------------------------------------------------------------------

typedef struct {
  Mat2 xform;
  float color[4];
  float lw;  // line width for path_stroke
} RState;

#define STATE_MAX 32

// ---------------------------------------------------------------------------
// Vertex types
// ---------------------------------------------------------------------------

typedef struct {
  float x, y, r, g, b, a;
} Vertex;

typedef struct {
  float x, y, u, v, r, g, b, a;
} TextVertex;

// ---------------------------------------------------------------------------
// Geometry batch
// ---------------------------------------------------------------------------

#define MAX_VERTS   16384
#define MAX_INDICES 32768

typedef struct {
  Vertex verts[MAX_VERTS];
  GLuint indices[MAX_INDICES];
  int nv, ni;
  GLuint vao, vbo, ebo;
  GLenum mode;
} Batch;

// ---------------------------------------------------------------------------
// Text batch
// ---------------------------------------------------------------------------

#define MAX_TV 8192
#define MAX_TI 16384

typedef struct {
  TextVertex verts[MAX_TV];
  GLuint indices[MAX_TI];
  int nv, ni;
  GLuint vao, vbo, ebo;
} TBatch;

// ---------------------------------------------------------------------------
// Glyph atlas
// ---------------------------------------------------------------------------

#define ATLAS_W    1024
#define ATLAS_H    1024
#define ATLAS_PAD  1
#define MAX_GLYPHS 1024

typedef struct {
  hb_codepoint_t id;
  float u0, v0, u1, v1;
  int bx, by, bw, bh;
} GlyphE;

typedef struct {
  GLuint tex;
  int px, py, rh;
  GlyphE cache[MAX_GLYPHS];
  int nc;
} Atlas;

// ---------------------------------------------------------------------------
// Path builder
// ---------------------------------------------------------------------------

#define MAX_PATH_STEPS 1024
#define MAX_FLAT       4096  // flattened polyline verts

typedef enum { PC_MOVE, PC_LINE, PC_CUBIC, PC_QUAD, PC_ARC, PC_CLOSE } PCmd;

typedef struct {
  PCmd cmd;
  float p[6];
} PStep;

typedef struct {
  PStep steps[MAX_PATH_STEPS];
  int ns;
  float sx, sy;  // path start (for CLOSE)
  float cx, cy;  // current pen
} Path;

// Tessellation scratch buffers (single-threaded)
static float g_flat[MAX_FLAT * 2];  // flattened polyline (screen space)
static int g_fn;                    // vertex count in g_flat
static int g_earidx[MAX_FLAT];
static GLuint g_eartri[MAX_FLAT * 3];

// ---------------------------------------------------------------------------
// Ticker  —  platform-independent; no GTK types inside
//
// Drive it from anywhere: GTK tick callback, a thread, a game loop, a test.
// GTK integration: the callback just calls ticker_tick() and queue_draw().
// Time source: POSIX clock_gettime(CLOCK_MONOTONIC) — not GdkFrameClock.
// ---------------------------------------------------------------------------

typedef void (*TickFn)(float t, float dt, void* ud);

typedef struct {
  double t0, tlast;  // absolute monotonic start / last-tick (seconds)
  float t, dt;       // elapsed / delta exposed to callers
  TickFn fn;
  void* ud;
  int running;
} Ticker;

static double mono_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void ticker_init(Ticker* tk, TickFn fn, void* ud) {
  memset(tk, 0, sizeof *tk);
  tk->fn = fn;
  tk->ud = ud;
}
static void ticker_start(Ticker* tk) {
  double n = mono_sec();
  tk->t0 = tk->tlast = n;
  tk->t = tk->dt = 0;
  tk->running = 1;
}
static void ticker_stop(Ticker* tk) {
  tk->running = 0;
}

// Call from any frame source — measures real time internally.
static void ticker_tick(Ticker* tk) {
  if (!tk->running)
    return;
  double n = mono_sec();
  tk->t = (float)(n - tk->t0);
  tk->dt = (float)(n - tk->tlast);
  tk->tlast = n;
  if (tk->fn)
    tk->fn(tk->t, tk->dt, tk->ud);
}

static float ticker_time(const Ticker* tk) {
  return tk->t;
}
static float ticker_dt(const Ticker* tk) {
  return tk->dt;
}

// ---------------------------------------------------------------------------
// Top-level renderer
// ---------------------------------------------------------------------------

typedef struct {
  GLuint prog_geom, prog_text;
  GLint loc_res_geom, loc_res_text, loc_atlas;

  Batch tris, lines;
  TBatch tbatch;
  Atlas atlas;

  FT_Library ftl;
  FT_Face ftf;
  hb_font_t* hbf;
  int font_ok;

  RState st[STATE_MAX];
  int stop;

  Path path;

  int w, h;
} Ren;

static Ren R;
static Ticker G_TICK;  // lives outside Ren — GTK-independent timer

// Current state shorthand
#define CS R.st[R.stop]

// Transform a local-space point to screen space
static void xp(float ix, float iy, float* ox, float* oy) {
  mat2_pt(CS.xform, ix, iy, ox, oy);
}

// ---------------------------------------------------------------------------
// GL helpers
// ---------------------------------------------------------------------------

static GLuint make_prog(const char* vs, const char* fs) {
  GLuint v = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(v, 1, &vs, NULL);
  glCompileShader(v);
  GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(f, 1, &fs, NULL);
  glCompileShader(f);
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}

// ---------------------------------------------------------------------------
// Geometry batch
// ---------------------------------------------------------------------------

static void batch_init(Batch* b, GLenum mode) {
  b->mode = mode;
  b->nv = b->ni = 0;
  glGenVertexArrays(1, &b->vao);
  glGenBuffers(1, &b->vbo);
  glGenBuffers(1, &b->ebo);
  glBindVertexArray(b->vao);
  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(b->verts), NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(b->indices), NULL, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
  glBindVertexArray(0);
}

static void batch_free(Batch* b) {
  glDeleteVertexArrays(1, &b->vao);
  glDeleteBuffers(1, &b->vbo);
  glDeleteBuffers(1, &b->ebo);
}

static void batch_flush(Batch* b) {
  if (!b->ni)
    return;
  glUseProgram(R.prog_geom);
  glUniform2f(R.loc_res_geom, (float)R.w, (float)R.h);
  glBindVertexArray(b->vao);
  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, b->nv * sizeof(Vertex), b->verts);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, b->ni * sizeof(GLuint), b->indices);
  glDrawElements(b->mode, b->ni, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
  b->nv = b->ni = 0;
}

static void batch_add(Batch* b, const Vertex* v, int nv, const GLuint* li, int ni) {
  if (b->nv + nv > MAX_VERTS || b->ni + ni > MAX_INDICES)
    batch_flush(b);
  GLuint base = (GLuint)b->nv;
  for (int i = 0; i < ni; i++)
    b->indices[b->ni + i] = li[i] + base;
  memcpy(&b->verts[b->nv], v, (size_t)nv * sizeof(Vertex));
  b->nv += nv;
  b->ni += ni;
}

// ---------------------------------------------------------------------------
// Text batch
// ---------------------------------------------------------------------------

static void tbatch_init(TBatch* b) {
  b->nv = b->ni = 0;
  glGenVertexArrays(1, &b->vao);
  glGenBuffers(1, &b->vbo);
  glGenBuffers(1, &b->ebo);
  glBindVertexArray(b->vao);
  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(b->verts), NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(b->indices), NULL, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, x));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, u));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)offsetof(TextVertex, r));
  glBindVertexArray(0);
}

static void tbatch_free(TBatch* b) {
  glDeleteVertexArrays(1, &b->vao);
  glDeleteBuffers(1, &b->vbo);
  glDeleteBuffers(1, &b->ebo);
}

static void tbatch_flush(TBatch* b) {
  if (!b->ni)
    return;
  glUseProgram(R.prog_text);
  glUniform2f(R.loc_res_text, (float)R.w, (float)R.h);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.atlas.tex);
  glUniform1i(R.loc_atlas, 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(b->vao);
  glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, b->nv * sizeof(TextVertex), b->verts);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, b->ni * sizeof(GLuint), b->indices);
  glDrawElements(GL_TRIANGLES, b->ni, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  b->nv = b->ni = 0;
}

static void tbatch_glyph(TBatch* b, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                         float cr, float cg, float cb, float ca) {
  if (b->nv + 4 > MAX_TV || b->ni + 6 > MAX_TI)
    tbatch_flush(b);
  GLuint base = (GLuint)b->nv;
  TextVertex vv[4] = {
      {x, y, u0, v0, cr, cg, cb, ca},
      {x + w, y, u1, v0, cr, cg, cb, ca},
      {x + w, y + h, u1, v1, cr, cg, cb, ca},
      {x, y + h, u0, v1, cr, cg, cb, ca},
  };
  GLuint ii[6] = {base, base + 1, base + 3, base + 1, base + 2, base + 3};
  memcpy(&b->verts[b->nv], vv, 4 * sizeof(TextVertex));
  memcpy(&b->indices[b->ni], ii, 6 * sizeof(GLuint));
  b->nv += 4;
  b->ni += 6;
}

// ---------------------------------------------------------------------------
// Glyph atlas
// ---------------------------------------------------------------------------

static void atlas_init(Atlas* a) {
  a->px = a->py = a->rh = a->nc = 0;
  glGenTextures(1, &a->tex);
  glBindTexture(GL_TEXTURE_2D, a->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void atlas_free(Atlas* a) {
  glDeleteTextures(1, &a->tex);
}

static void atlas_clear(Atlas* a) {
  a->px = a->py = a->rh = a->nc = 0;
  glBindTexture(GL_TEXTURE_2D, a->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static GlyphE* atlas_glyph(Atlas* a, hb_codepoint_t gid) {
  for (int i = 0; i < a->nc; i++)
    if (a->cache[i].id == gid)
      return &a->cache[i];
  if (a->nc >= MAX_GLYPHS)
    return NULL;
  if (FT_Load_Glyph(R.ftf, gid, FT_LOAD_RENDER))
    return NULL;
  FT_GlyphSlot sl = R.ftf->glyph;
  FT_Bitmap* bm = &sl->bitmap;
  GlyphE* e = &a->cache[a->nc++];
  e->id = gid;
  e->bx = sl->bitmap_left;
  e->by = sl->bitmap_top;
  e->bw = (int)bm->width;
  e->bh = (int)bm->rows;
  if (!e->bw || !e->bh) {
    e->u0 = e->v0 = e->u1 = e->v1 = 0;
    return e;
  }
  if (a->px + e->bw + ATLAS_PAD > ATLAS_W) {
    a->py += a->rh + ATLAS_PAD;
    a->px = 0;
    a->rh = 0;
  }
  if (a->py + e->bh > ATLAS_H) {
    e->u0 = e->v0 = e->u1 = e->v1 = 0;
    return e;
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, abs(bm->pitch));
  glBindTexture(GL_TEXTURE_2D, a->tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, a->px, a->py, e->bw, e->bh, GL_RED, GL_UNSIGNED_BYTE, bm->buffer);
  glBindTexture(GL_TEXTURE_2D, 0);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  e->u0 = (float)a->px / ATLAS_W;
  e->v0 = (float)a->py / ATLAS_H;
  e->u1 = (float)(a->px + e->bw) / ATLAS_W;
  e->v1 = (float)(a->py + e->bh) / ATLAS_H;
  if (e->bh > a->rh)
    a->rh = e->bh;
  a->px += e->bw + ATLAS_PAD;
  return e;
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

static const char* kFonts[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                               "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
                               "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                               "/usr/share/fonts/TTF/DejaVuSans.ttf",
                               "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                               "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                               NULL};

static int font_init(int px) {
  if (FT_Init_FreeType(&R.ftl))
    return 0;
  for (int i = 0; kFonts[i]; i++) {
    if (!FT_New_Face(R.ftl, kFonts[i], 0, &R.ftf)) {
      fprintf(stderr, "Font: %s\n", kFonts[i]);
      FT_Set_Pixel_Sizes(R.ftf, 0, (FT_UInt)px);
      R.hbf = hb_ft_font_create(R.ftf, NULL);
      return 1;
    }
  }
  FT_Done_FreeType(R.ftl);
  return 0;
}

static void font_free(void) {
  if (R.hbf) {
    hb_font_destroy(R.hbf);
    R.hbf = NULL;
  }
  if (R.ftf) {
    FT_Done_Face(R.ftf);
    R.ftf = NULL;
  }
  FT_Done_FreeType(R.ftl);
}

// ---------------------------------------------------------------------------
// Flush all batches (geometry first, text on top)
// ---------------------------------------------------------------------------

static void ren_flush(void) {
  batch_flush(&R.tris);
  batch_flush(&R.lines);
  tbatch_flush(&R.tbatch);
}

// ---------------------------------------------------------------------------
// Path: building
// ---------------------------------------------------------------------------

static void path_push_step(PCmd cmd, float a, float b, float c, float d, float e, float f) {
  if (R.path.ns >= MAX_PATH_STEPS)
    return;
  PStep* s = &R.path.steps[R.path.ns++];
  s->cmd = cmd;
  s->p[0] = a;
  s->p[1] = b;
  s->p[2] = c;
  s->p[3] = d;
  s->p[4] = e;
  s->p[5] = f;
}

void render_path_begin(void) {
  R.path.ns = 0;
  R.path.cx = R.path.cy = R.path.sx = R.path.sy = 0;
}

void render_path_move_to(float x, float y) {
  path_push_step(PC_MOVE, x, y, 0, 0, 0, 0);
  R.path.cx = x;
  R.path.cy = y;
  R.path.sx = x;
  R.path.sy = y;
}

void render_path_line_to(float x, float y) {
  path_push_step(PC_LINE, x, y, 0, 0, 0, 0);
  R.path.cx = x;
  R.path.cy = y;
}

void render_path_cubic_to(float cx1, float cy1, float cx2, float cy2, float x, float y) {
  path_push_step(PC_CUBIC, cx1, cy1, cx2, cy2, x, y);
  R.path.cx = x;
  R.path.cy = y;
}

void render_path_quad_to(float cx, float cy, float x, float y) {
  path_push_step(PC_QUAD, cx, cy, x, y, 0, 0);
  R.path.cx = x;
  R.path.cy = y;
}

// Arc from a0 to a1 (radians) on a circle at (cx,cy) with radius r
void render_path_arc(float cx, float cy, float r, float a0, float a1) {
  path_push_step(PC_ARC, cx, cy, r, a0, a1, 0);
  R.path.cx = cx + cosf(a1) * r;
  R.path.cy = cy + sinf(a1) * r;
}

void render_path_close(void) {
  path_push_step(PC_CLOSE, 0, 0, 0, 0, 0, 0);
  R.path.cx = R.path.sx;
  R.path.cy = R.path.sy;
}

// ---------------------------------------------------------------------------
// Path: flattening to screen-space polyline
// ---------------------------------------------------------------------------

static void flat_push(float x, float y) {
  if (g_fn >= MAX_FLAT)
    return;
  float sx, sy;
  xp(x, y, &sx, &sy);
  g_flat[g_fn * 2] = sx;
  g_flat[g_fn * 2 + 1] = sy;
  g_fn++;
}

// Recursive cubic bezier subdivision (de Casteljau)
static void flat_cubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, int depth) {
  if (depth > 10) {
    flat_push(x3, y3);
    return;
  }
  float dx = x3 - x0, dy = y3 - y0, d = sqrtf(dx * dx + dy * dy) + 1e-5f;
  float d1 = fabsf((x1 - x0) * dy - (y1 - y0) * dx);
  float d2 = fabsf((x2 - x0) * dy - (y2 - y0) * dx);
  if ((d1 + d2) / d < 1.5f) {
    flat_push(x3, y3);
    return;
  }
  float m01x = (x0 + x1) * .5f, m01y = (y0 + y1) * .5f;
  float m12x = (x1 + x2) * .5f, m12y = (y1 + y2) * .5f;
  float m23x = (x2 + x3) * .5f, m23y = (y2 + y3) * .5f;
  float m012x = (m01x + m12x) * .5f, m012y = (m01y + m12y) * .5f;
  float m123x = (m12x + m23x) * .5f, m123y = (m12y + m23y) * .5f;
  float mx = (m012x + m123x) * .5f, my = (m012y + m123y) * .5f;
  flat_cubic(x0, y0, m01x, m01y, m012x, m012y, mx, my, depth + 1);
  flat_cubic(mx, my, m123x, m123y, m23x, m23y, x3, y3, depth + 1);
}

// Recursive quadratic bezier subdivision
static void flat_quad(float x0, float y0, float cx, float cy, float x1, float y1, int depth) {
  if (depth > 10) {
    flat_push(x1, y1);
    return;
  }
  float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy) + 1e-5f;
  float dd = fabsf((cx - x0) * dy - (cy - y0) * dx);
  if (dd / d < 1.0f) {
    flat_push(x1, y1);
    return;
  }
  float m0x = (x0 + cx) * .5f, m0y = (y0 + cy) * .5f;
  float m1x = (cx + x1) * .5f, m1y = (cy + y1) * .5f;
  float mx = (m0x + m1x) * .5f, my = (m0y + m1y) * .5f;
  flat_quad(x0, y0, m0x, m0y, mx, my, depth + 1);
  flat_quad(mx, my, m1x, m1y, x1, y1, depth + 1);
}

// Flatten the current path into g_flat[]. Returns 1 if closed.
static int path_flatten(void) {
  g_fn = 0;
  int closed = 0;
  float px = 0, py = 0;
  for (int i = 0; i < R.path.ns; i++) {
    PStep* s = &R.path.steps[i];
    switch (s->cmd) {
      case PC_MOVE:
        if (g_fn > 0) { /* sub-path break; for now just continue */
        }
        flat_push(s->p[0], s->p[1]);
        px = s->p[0];
        py = s->p[1];
        break;
      case PC_LINE:
        flat_push(s->p[0], s->p[1]);
        px = s->p[0];
        py = s->p[1];
        break;
      case PC_CUBIC:
        flat_cubic(px, py, s->p[0], s->p[1], s->p[2], s->p[3], s->p[4], s->p[5], 0);
        px = s->p[4];
        py = s->p[5];
        break;
      case PC_QUAD:
        flat_quad(px, py, s->p[0], s->p[1], s->p[2], s->p[3], 0);
        px = s->p[2];
        py = s->p[3];
        break;
      case PC_ARC: {
        float cx = s->p[0], cy = s->p[1], r = s->p[2], a0 = s->p[3], a1 = s->p[4];
        float span = a1 - a0;
        int n = (int)(fabsf(span) * r / 3.0f) + 4;
        if (n > 128)
          n = 128;
        for (int j = (g_fn > 0 ? 1 : 0); j <= n; j++) {
          float t = (float)j / n;
          flat_push(cx + cosf(a0 + span * t) * r, cy + sinf(a0 + span * t) * r);
        }
        px = cx + cosf(a1) * r;
        py = cy + sinf(a1) * r;
        break;
      }
      case PC_CLOSE:
        closed = 1;
        break;
    }
  }
  return closed;
}

// ---------------------------------------------------------------------------
// Path: ear-clip fill
// ---------------------------------------------------------------------------

static void path_earclip_fill(void) {
  int n = g_fn;
  if (n < 3)
    return;
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];

  // Compute signed area (shoelace) to determine winding
  float area = 0;
  for (int i = 0, j = n - 1; i < n; j = i++)
    area += g_flat[j * 2] * g_flat[i * 2 + 1] - g_flat[i * 2] * g_flat[j * 2 + 1];
  float sign = (area >= 0) ? 1.f : -1.f;

  for (int i = 0; i < n; i++)
    g_earidx[i] = i;
  int rem = n, ntri = 0;

  for (int iter = 0; iter < n * n && rem > 3; iter++) {
    int found = 0;
    for (int i = 0; i < rem; i++) {
      int p = (i - 1 + rem) % rem, c = i, np = (i + 1) % rem;
      float ax = g_flat[g_earidx[p] * 2], ay = g_flat[g_earidx[p] * 2 + 1];
      float bx = g_flat[g_earidx[c] * 2], by = g_flat[g_earidx[c] * 2 + 1];
      float cx2 = g_flat[g_earidx[np] * 2], cy2 = g_flat[g_earidx[np] * 2 + 1];
      float cross = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
      if (cross * sign <= 0)
        continue;
      int ear = 1;
      for (int j = 0; j < rem && ear; j++) {
        if (j == p || j == c || j == np)
          continue;
        float qx = g_flat[g_earidx[j] * 2], qy = g_flat[g_earidx[j] * 2 + 1];
        float d1 = (bx - ax) * (qy - ay) - (by - ay) * (qx - ax);
        float d2 = (cx2 - bx) * (qy - by) - (cy2 - by) * (qx - bx);
        float d3 = (ax - cx2) * (qy - cy2) - (ay - cy2) * (qx - cx2);
        if (d1 * sign > 0 && d2 * sign > 0 && d3 * sign > 0)
          ear = 0;
      }
      if (ear) {
        g_eartri[ntri * 3] = g_earidx[p];
        g_eartri[ntri * 3 + 1] = g_earidx[c];
        g_eartri[ntri * 3 + 2] = g_earidx[np];
        ntri++;
        for (int j = c; j < rem - 1; j++)
          g_earidx[j] = g_earidx[j + 1];
        rem--;
        found = 1;
        break;
      }
    }
    if (!found)
      break;
  }
  if (rem == 3) {
    g_eartri[ntri * 3] = g_earidx[0];
    g_eartri[ntri * 3 + 1] = g_earidx[1];
    g_eartri[ntri * 3 + 2] = g_earidx[2];
    ntri++;
  }

  // Build vertex array from polygon points and submit
  Vertex* vv = (Vertex*)alloca(n * sizeof(Vertex));
  for (int i = 0; i < n; i++)
    vv[i] = (Vertex){g_flat[i * 2], g_flat[i * 2 + 1], cr, cg, cb, ca};
  batch_add(&R.tris, vv, n, g_eartri, ntri * 3);
}

// ---------------------------------------------------------------------------
// Path: miter-join stroke
// ---------------------------------------------------------------------------

#define MITER_LIMIT 4.0f

static void path_miter_stroke(int closed) {
  int n = g_fn;
  if (n < 2)
    return;
  float hw = CS.lw * 0.5f;
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];

  // Per-vertex miter normals (left/right offset points)
  float *lx = (float*)alloca(n * sizeof(float)), *ly = (float*)alloca(n * sizeof(float));
  float *rx = (float*)alloca(n * sizeof(float)), *ry = (float*)alloca(n * sizeof(float));

  for (int i = 0; i < n; i++) {
    int has_p = closed || (i > 0);
    int has_n = closed || (i < n - 1);
    float n0x = 0, n0y = 0, n1x = 0, n1y = 0;
    if (has_p) {
      int pi = closed ? (i - 1 + n) % n : i - 1;
      float dx = g_flat[i * 2] - g_flat[pi * 2], dy = g_flat[i * 2 + 1] - g_flat[pi * 2 + 1];
      float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
      n0x = -dy / len;
      n0y = dx / len;
    }
    if (has_n) {
      int ni2 = closed ? (i + 1) % n : i + 1;
      float dx = g_flat[ni2 * 2] - g_flat[i * 2], dy = g_flat[ni2 * 2 + 1] - g_flat[i * 2 + 1];
      float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
      n1x = -dy / len;
      n1y = dx / len;
    }
    float bx, by, sc;
    if (has_p && has_n) {
      bx = n0x + n1x;
      by = n0y + n1y;
      float bl = sqrtf(bx * bx + by * by);
      if (bl > 1e-6f) {
        bx /= bl;
        by /= bl;
        float dot = bx * n1x + by * n1y;
        sc = (fabsf(dot) > 1e-6f) ? 1.0f / dot : MITER_LIMIT;
        if (fabsf(sc) > MITER_LIMIT)
          sc = MITER_LIMIT * (sc < 0 ? -1.f : 1.f);
      } else {
        bx = n0x;
        by = n0y;
        sc = 1;
      }
    } else if (has_p) {
      bx = n0x;
      by = n0y;
      sc = 1;
    } else {
      bx = n1x;
      by = n1y;
      sc = 1;
    }
    lx[i] = g_flat[i * 2] + bx * hw * sc;
    ly[i] = g_flat[i * 2 + 1] + by * hw * sc;
    rx[i] = g_flat[i * 2] - bx * hw * sc;
    ry[i] = g_flat[i * 2 + 1] - by * hw * sc;
  }

  int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; i++) {
    int j = closed ? (i + 1) % n : i + 1;
    Vertex vv[4] = {
        {lx[i], ly[i], cr, cg, cb, ca},
        {lx[j], ly[j], cr, cg, cb, ca},
        {rx[j], ry[j], cr, cg, cb, ca},
        {rx[i], ry[i], cr, cg, cb, ca},
    };
    GLuint ii[6] = {0, 1, 3, 1, 2, 3};
    batch_add(&R.tris, vv, 4, ii, 6);
  }
}

// ---------------------------------------------------------------------------
// Polygon fill helper (ear-clip on a local-space polygon after transform)
// ---------------------------------------------------------------------------

static void fill_polygon_raw(float* pts, int n) {
  // Transform into g_flat
  g_fn = n;
  for (int i = 0; i < n; i++)
    xp(pts[i * 2], pts[i * 2 + 1], &g_flat[i * 2], &g_flat[i * 2 + 1]);
  path_earclip_fill();
}

// ---------------------------------------------------------------------------
// Renderer init / free
// ---------------------------------------------------------------------------

static void ren_init(void) {
  R.prog_geom = make_prog(kGeomVert, kGeomFrag);
  R.prog_text = make_prog(kTextVert, kTextFrag);
  R.loc_res_geom = glGetUniformLocation(R.prog_geom, "u_res");
  R.loc_res_text = glGetUniformLocation(R.prog_text, "u_res");
  R.loc_atlas = glGetUniformLocation(R.prog_text, "u_atlas");
  batch_init(&R.tris, GL_TRIANGLES);
  batch_init(&R.lines, GL_LINES);
  tbatch_init(&R.tbatch);
  atlas_init(&R.atlas);
  R.stop = 0;
  R.st[0] = (RState){mat2_id(), {1, 1, 1, 1}, 2.0f};
  R.font_ok = font_init(18);
}

static void ren_free(void) {
  if (R.font_ok)
    font_free();
  atlas_free(&R.atlas);
  tbatch_free(&R.tbatch);
  batch_free(&R.lines);
  batch_free(&R.tris);
  glDeleteProgram(R.prog_text);
  glDeleteProgram(R.prog_geom);
}

// ---------------------------------------------------------------------------
// Public API — frame
// ---------------------------------------------------------------------------

void render_begin(int w, int h) {
  R.w = w;
  R.h = h;
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
}

void render_end(void) {
  ren_flush();
  glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------
// Public API — state
// ---------------------------------------------------------------------------

void render_push(void) {
  if (R.stop < STATE_MAX - 1) {
    R.st[R.stop + 1] = R.st[R.stop];
    R.stop++;
  }
}

void render_pop(void) {
  if (R.stop > 0)
    R.stop--;
}

void render_identity(void) {
  CS.xform = mat2_id();
}

void render_translate(float x, float y) {
  mat2_translate(&CS.xform, x, y);
}

void render_rotate(float a) {
  mat2_rotate(&CS.xform, a);
}

void render_scale(float sx, float sy) {
  mat2_scale(&CS.xform, sx, sy);
}

void render_set_color(float r, float g, float b, float a) {
  CS.color[0] = r;
  CS.color[1] = g;
  CS.color[2] = b;
  CS.color[3] = a;
}

void render_set_line_width(float w) {
  CS.lw = w;
}

// ---------------------------------------------------------------------------
// Public API — clip
// ---------------------------------------------------------------------------

void render_clip_rect(float x, float y, float w, float h) {
  ren_flush();
  glEnable(GL_SCISSOR_TEST);
  glScissor((int)x, R.h - (int)(y + h), (int)w, (int)h);
}
void render_clip_reset(void) {
  ren_flush();
  glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------
// Public API — basic primitives (transformed)
// ---------------------------------------------------------------------------

void render_fill_rect(float x, float y, float w, float h) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  float x0, y0, x1, y1, x2, y2, x3, y3;
  xp(x, y, &x0, &y0);
  xp(x + w, y, &x1, &y1);
  xp(x + w, y + h, &x2, &y2);
  xp(x, y + h, &x3, &y3);
  Vertex vv[4] = {
      {x0, y0, cr, cg, cb, ca}, {x1, y1, cr, cg, cb, ca}, {x2, y2, cr, cg, cb, ca}, {x3, y3, cr, cg, cb, ca}};
  GLuint ii[6] = {0, 1, 3, 1, 2, 3};
  batch_add(&R.tris, vv, 4, ii, 6);
}

void render_draw_rect(float x, float y, float w, float h) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  float x0, y0, x1, y1, x2, y2, x3, y3;
  xp(x, y, &x0, &y0);
  xp(x + w, y, &x1, &y1);
  xp(x + w, y + h, &x2, &y2);
  xp(x, y + h, &x3, &y3);
  Vertex vv[4] = {
      {x0, y0, cr, cg, cb, ca}, {x1, y1, cr, cg, cb, ca}, {x2, y2, cr, cg, cb, ca}, {x3, y3, cr, cg, cb, ca}};
  GLuint ii[8] = {0, 1, 1, 2, 2, 3, 3, 0};
  batch_add(&R.lines, vv, 4, ii, 8);
}

void render_draw_line(float x0, float y0, float x1, float y1) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  float sx0, sy0, sx1, sy1;
  xp(x0, y0, &sx0, &sy0);
  xp(x1, y1, &sx1, &sy1);
  Vertex vv[2] = {{sx0, sy0, cr, cg, cb, ca}, {sx1, sy1, cr, cg, cb, ca}};
  GLuint ii[2] = {0, 1};
  batch_add(&R.lines, vv, 2, ii, 2);
}

// ---------------------------------------------------------------------------
// Circles and ellipses
// ---------------------------------------------------------------------------

#define CIRCLE_SEGS 48

void render_fill_circle(float cx, float cy, float r) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  Vertex vv[CIRCLE_SEGS + 1];
  GLuint ii[CIRCLE_SEGS * 3];
  float sx, sy;
  xp(cx, cy, &sx, &sy);
  vv[0] = (Vertex){sx, sy, cr, cg, cb, ca};
  for (int i = 0; i < CIRCLE_SEGS; i++) {
    float a = (float)i / CIRCLE_SEGS * 2.f * (float)M_PI;
    xp(cx + cosf(a) * r, cy + sinf(a) * r, &vv[i + 1].x, &vv[i + 1].y);
    vv[i + 1].r = cr;
    vv[i + 1].g = cg;
    vv[i + 1].b = cb;
    vv[i + 1].a = ca;
    ii[i * 3] = 0;
    ii[i * 3 + 1] = i + 1;
    ii[i * 3 + 2] = (i + 1) % CIRCLE_SEGS + 1;
  }
  batch_add(&R.tris, vv, CIRCLE_SEGS + 1, ii, CIRCLE_SEGS * 3);
}

void render_draw_circle(float cx, float cy, float r) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  Vertex vv[CIRCLE_SEGS];
  GLuint ii[CIRCLE_SEGS * 2];
  for (int i = 0; i < CIRCLE_SEGS; i++) {
    float a = (float)i / CIRCLE_SEGS * 2.f * (float)M_PI;
    xp(cx + cosf(a) * r, cy + sinf(a) * r, &vv[i].x, &vv[i].y);
    vv[i].r = cr;
    vv[i].g = cg;
    vv[i].b = cb;
    vv[i].a = ca;
    ii[i * 2] = i;
    ii[i * 2 + 1] = (i + 1) % CIRCLE_SEGS;
  }
  batch_add(&R.lines, vv, CIRCLE_SEGS, ii, CIRCLE_SEGS * 2);
}

void render_fill_ellipse(float cx, float cy, float rx, float ry) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  Vertex vv[CIRCLE_SEGS + 1];
  GLuint ii[CIRCLE_SEGS * 3];
  float sx, sy;
  xp(cx, cy, &sx, &sy);
  vv[0] = (Vertex){sx, sy, cr, cg, cb, ca};
  for (int i = 0; i < CIRCLE_SEGS; i++) {
    float a = (float)i / CIRCLE_SEGS * 2.f * (float)M_PI;
    xp(cx + cosf(a) * rx, cy + sinf(a) * ry, &vv[i + 1].x, &vv[i + 1].y);
    vv[i + 1].r = cr;
    vv[i + 1].g = cg;
    vv[i + 1].b = cb;
    vv[i + 1].a = ca;
    ii[i * 3] = 0;
    ii[i * 3 + 1] = i + 1;
    ii[i * 3 + 2] = (i + 1) % CIRCLE_SEGS + 1;
  }
  batch_add(&R.tris, vv, CIRCLE_SEGS + 1, ii, CIRCLE_SEGS * 3);
}

// ---------------------------------------------------------------------------
// Rounded rectangle  (convex, so fan-triangulation is correct)
// ---------------------------------------------------------------------------

#define CORNER_SEGS 12

static void rounded_rect_pts(float x, float y, float w, float h, float rad, float* pts, int* np) {
  float r = rad;
  if (r > w * .5f)
    r = w * .5f;
  if (r > h * .5f)
    r = h * .5f;
  *np = 0;
  float cx[4] = {x + r, x + w - r, x + w - r, x + r};
  float cy[4] = {y + r, y + r, y + h - r, y + h - r};
  float a0[4] = {(float)M_PI, 1.5f * (float)M_PI, 0, .5f * (float)M_PI};
  for (int c = 0; c < 4; c++) {
    for (int j = 0; j <= CORNER_SEGS; j++) {
      float a = a0[c] + (float)j / CORNER_SEGS * .5f * (float)M_PI;
      pts[(*np) * 2] = cx[c] + cosf(a) * r;
      pts[(*np) * 2 + 1] = cy[c] + sinf(a) * r;
      (*np)++;
    }
  }
}

void render_fill_rounded_rect(float x, float y, float w, float h, float r) {
  float pts[(CORNER_SEGS + 1) * 4 * 2];
  int np;
  rounded_rect_pts(x, y, w, h, r, pts, &np);
  fill_polygon_raw(pts, np);
}

void render_draw_rounded_rect(float x, float y, float w, float h, float rad) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  float pts[(CORNER_SEGS + 1) * 4 * 2];
  int np;
  rounded_rect_pts(x, y, w, h, rad, pts, &np);
  Vertex* vv = (Vertex*)alloca(np * sizeof(Vertex));
  GLuint* ii = (GLuint*)alloca(np * 2 * sizeof(GLuint));
  for (int i = 0; i < np; i++) {
    xp(pts[i * 2], pts[i * 2 + 1], &vv[i].x, &vv[i].y);
    vv[i].r = cr;
    vv[i].g = cg;
    vv[i].b = cb;
    vv[i].a = ca;
    ii[i * 2] = i;
    ii[i * 2 + 1] = (i + 1) % np;
  }
  batch_add(&R.lines, vv, np, ii, np * 2);
}

// ---------------------------------------------------------------------------
// Polygon
// ---------------------------------------------------------------------------

void render_fill_polygon(float* pts, int n) {
  fill_polygon_raw(pts, n);
}

void render_draw_polygon(float* pts, int n) {
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  Vertex* vv = (Vertex*)alloca(n * sizeof(Vertex));
  GLuint* ii = (GLuint*)alloca(n * 2 * sizeof(GLuint));
  for (int i = 0; i < n; i++) {
    xp(pts[i * 2], pts[i * 2 + 1], &vv[i].x, &vv[i].y);
    vv[i].r = cr;
    vv[i].g = cg;
    vv[i].b = cb;
    vv[i].a = ca;
    ii[i * 2] = i;
    ii[i * 2 + 1] = (i + 1) % n;
  }
  batch_add(&R.lines, vv, n, ii, n * 2);
}

// ---------------------------------------------------------------------------
// Public API — paths
// ---------------------------------------------------------------------------

void render_path_fill(void) {
  path_flatten();
  path_earclip_fill();
}

void render_path_stroke(void) {
  int closed = path_flatten();
  path_miter_stroke(closed);
}

// ---------------------------------------------------------------------------
// Public API — text
// ---------------------------------------------------------------------------

void render_set_font_size(int px) {
  if (!R.font_ok)
    return;
  FT_Set_Pixel_Sizes(R.ftf, 0, (FT_UInt)px);
  hb_font_destroy(R.hbf);
  R.hbf = hb_ft_font_create(R.ftf, NULL);
  atlas_clear(&R.atlas);
}

void render_draw_text(float x, float y, const char* text) {
  if (!R.font_ok || !text || !*text)
    return;
  hb_buffer_t* buf = hb_buffer_create();
  hb_buffer_add_utf8(buf, text, -1, 0, -1);
  hb_buffer_guess_segment_properties(buf);
  hb_shape(R.hbf, buf, NULL, 0);
  unsigned int cnt;
  hb_glyph_info_t* gi = hb_buffer_get_glyph_infos(buf, &cnt);
  hb_glyph_position_t* gp = hb_buffer_get_glyph_positions(buf, &cnt);
  float cr = CS.color[0], cg = CS.color[1], cb = CS.color[2], ca = CS.color[3];
  float asc = (float)(R.ftf->size->metrics.ascender >> 6);
  float sx, sy;
  xp(x, y, &sx, &sy);
  float px = sx, py = sy + asc;
  for (unsigned int i = 0; i < cnt; i++) {
    float xo = gp[i].x_offset / 64.f, yo = gp[i].y_offset / 64.f;
    float xa = gp[i].x_advance / 64.f, ya = gp[i].y_advance / 64.f;
    GlyphE* e = atlas_glyph(&R.atlas, gi[i].codepoint);
    if (e && e->bw > 0 && e->bh > 0)
      tbatch_glyph(&R.tbatch, px + xo + e->bx, py - yo - e->by, e->bw, e->bh, e->u0, e->v0, e->u1, e->v1, cr, cg, cb,
                   ca);
    px += xa;
    py -= ya;
  }
  hb_buffer_destroy(buf);
}

float render_measure_text(const char* text) {
  if (!R.font_ok || !text || !*text)
    return 0;
  hb_buffer_t* buf = hb_buffer_create();
  hb_buffer_add_utf8(buf, text, -1, 0, -1);
  hb_buffer_guess_segment_properties(buf);
  hb_shape(R.hbf, buf, NULL, 0);
  unsigned int cnt;
  hb_glyph_position_t* gp = hb_buffer_get_glyph_positions(buf, &cnt);
  float w = 0;
  for (unsigned int i = 0; i < cnt; i++)
    w += gp[i].x_advance / 64.f;
  hb_buffer_destroy(buf);
  return w;
}

// ---------------------------------------------------------------------------
// GTK shim for Ticker
// The frame clock fires at vsync; we just use it as the "time to tick" signal.
// GdkFrameClock is NOT used for time — clock_gettime inside ticker_tick is.
// ---------------------------------------------------------------------------

static gboolean gtk_tick_cb(GtkWidget* w, GdkFrameClock* clk, gpointer ud) {
  ticker_tick((Ticker*)ud);  // measures real time internally
  gtk_widget_queue_draw(w);
  return G_SOURCE_CONTINUE;
  (void)clk;  // unused — time comes from clock_gettime
}

static guint gtk_ticker_attach(Ticker* tk, GtkWidget* w) {
  return gtk_widget_add_tick_callback(w, gtk_tick_cb, tk, NULL);
}
static void gtk_ticker_detach(Ticker* tk, GtkWidget* w, guint id) {
  ticker_stop(tk);
  gtk_widget_remove_tick_callback(w, id);
}

// ---------------------------------------------------------------------------
// Easing / math utilities
// ---------------------------------------------------------------------------

float lerp_f(float a, float b, float t) {
  return a + (b - a) * t;
}
float clampf(float x, float lo, float hi) {
  return x < lo ? lo : x > hi ? hi : x;
}
float ease_smooth(float t) {
  t = clampf(t, 0, 1);
  return t * t * (3.f - 2.f * t);
}
float ease_in(float t) {
  t = clampf(t, 0, 1);
  return t * t;
}
float ease_out(float t) {
  t = clampf(t, 0, 1);
  return 1.f - (1.f - t) * (1.f - t);
}
float ease_in_out(float t) {
  t = clampf(t, 0, 1);
  return t < .5f ? 2 * t * t : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * .5f;
}
// Oscillates 0→1→0 over [period] seconds
float ping_pong(float t, float period) {
  float p = fmodf(t, period) / period;
  return p < .5f ? p * 2.f : 2.f - p * 2.f;
}

// ---------------------------------------------------------------------------
// Bezier curves
// ---------------------------------------------------------------------------

// Cubic bezier segment: two endpoints + two off-curve control points
typedef struct {
  float x0, y0, cx1, cy1, cx2, cy2, x1, y1;
} Bezier;
// Quadratic bezier segment: two endpoints + one control point
typedef struct {
  float x0, y0, cx, cy, x1, y1;
} QuadBez;

// --- Cubic ---

// Position at parameter t ∈ [0,1]
static void bezier_pt(const Bezier* b, float t, float* px, float* py) {
  float u = 1 - t, uu = u * u, tt = t * t, uuu = uu * u, ttt = tt * t;
  *px = uuu * b->x0 + 3 * uu * t * b->cx1 + 3 * u * tt * b->cx2 + ttt * b->x1;
  *py = uuu * b->y0 + 3 * uu * t * b->cy1 + 3 * u * tt * b->cy2 + ttt * b->y1;
}

// Tangent (first derivative) at t — not normalised
static void bezier_tan(const Bezier* b, float t, float* tx, float* ty) {
  float u = 1 - t, a = 3 * u * u, bv = 6 * u * t, c = 3 * t * t;
  *tx = a * (b->cx1 - b->x0) + bv * (b->cx2 - b->cx1) + c * (b->x1 - b->cx2);
  *ty = a * (b->cy1 - b->y0) + bv * (b->cy2 - b->cy1) + c * (b->y1 - b->cy2);
}

// Unit normal (perpendicular to tangent, pointing left)
static void bezier_normal(const Bezier* b, float t, float* nx, float* ny) {
  float tx, ty;
  bezier_tan(b, t, &tx, &ty);
  float len = sqrtf(tx * tx + ty * ty) + 1e-6f;
  *nx = -ty / len;
  *ny = tx / len;
}

// De Casteljau split: divide b at t into left and right sub-curves.
// Both halves are valid cubic beziers — no approximation involved.
static void bezier_split(const Bezier* b, float t, Bezier* L, Bezier* R) {
  float q0x = lerp_f(b->x0, b->cx1, t), q0y = lerp_f(b->y0, b->cy1, t);
  float q1x = lerp_f(b->cx1, b->cx2, t), q1y = lerp_f(b->cy1, b->cy2, t);
  float q2x = lerp_f(b->cx2, b->x1, t), q2y = lerp_f(b->cy2, b->y1, t);
  float r0x = lerp_f(q0x, q1x, t), r0y = lerp_f(q0y, q1y, t);
  float r1x = lerp_f(q1x, q2x, t), r1y = lerp_f(q1y, q2y, t);
  float mx = lerp_f(r0x, r1x, t), my = lerp_f(r0y, r1y, t);
  *L = (Bezier){b->x0, b->y0, q0x, q0y, r0x, r0y, mx, my};
  *R = (Bezier){mx, my, r1x, r1y, q2x, q2y, b->x1, b->y1};
  // Store intermediates for callers that want to draw the construction
  // (q0,q1,q2) = level-1 points; (r0,r1) = level-2; mx,my = split point
  (void)q0x;
  (void)q0y;
  (void)q1x;
  (void)q1y;
  (void)q2x;
  (void)q2y;
  (void)r0x;
  (void)r0y;
  (void)r1x;
  (void)r1y;
}

// Same as bezier_split but also returns the 5 de Casteljau construction pts
static void bezier_split_dbg(const Bezier* b, float t, Bezier* L, Bezier* R, float* q0x, float* q0y,  // edge midpoints
                             float* q1x, float* q1y, float* q2x, float* q2y, float* r0x,
                             float* r0y,  // second-level mids
                             float* r1x, float* r1y) {
  *q0x = lerp_f(b->x0, b->cx1, t);
  *q0y = lerp_f(b->y0, b->cy1, t);
  *q1x = lerp_f(b->cx1, b->cx2, t);
  *q1y = lerp_f(b->cy1, b->cy2, t);
  *q2x = lerp_f(b->cx2, b->x1, t);
  *q2y = lerp_f(b->cy2, b->y1, t);
  *r0x = lerp_f(*q0x, *q1x, t);
  *r0y = lerp_f(*q0y, *q1y, t);
  *r1x = lerp_f(*q1x, *q2x, t);
  *r1y = lerp_f(*q1y, *q2y, t);
  float mx = lerp_f(*r0x, *r1x, t), my = lerp_f(*r0y, *r1y, t);
  *L = (Bezier){b->x0, b->y0, *q0x, *q0y, *r0x, *r0y, mx, my};
  *R = (Bezier){mx, my, *r1x, *r1y, *q2x, *q2y, b->x1, b->y1};
}

// Piecewise-linear arc-length estimate (steps=20 is fine for display)
static float bezier_length(const Bezier* b, int steps) {
  float len = 0, px = b->x0, py = b->y0;
  for (int i = 1; i <= steps; i++) {
    float qx, qy;
    bezier_pt(b, (float)i / steps, &qx, &qy);
    float dx = qx - px, dy = qy - py;
    len += sqrtf(dx * dx + dy * dy);
    px = qx;
    py = qy;
  }
  return len;
}

// --- Quadratic ---

static void qbezier_pt(const QuadBez* b, float t, float* px, float* py) {
  float u = 1 - t, uu = u * u, tt = t * t;
  *px = uu * b->x0 + 2 * u * t * b->cx + tt * b->x1;
  *py = uu * b->y0 + 2 * u * t * b->cy + tt * b->y1;
}

static void qbezier_tan(const QuadBez* b, float t, float* tx, float* ty) {
  float u = 1 - t;
  *tx = 2 * (u * (b->cx - b->x0) + t * (b->x1 - b->cx));
  *ty = 2 * (u * (b->cy - b->y0) + t * (b->y1 - b->cy));
}

// --- CSS cubic-bezier timing function ---
//
// Equivalent to CSS cubic-bezier(p1x,p1y,p2x,p2y).
// Anchor points are fixed at (0,0) and (1,1); control points are (p1x,p1y)
// and (p2x,p2y). Given input x ∈ [0,1], solves Bx(t)=x via Newton iteration
// then returns By(t). Converges in <10 iterations for well-formed curves.
static float bezier_ease(float p1x, float p1y, float p2x, float p2y, float x) {
  if (x <= 0)
    return 0;
  if (x >= 1)
    return 1;
  float t = x;  // initial guess
  for (int i = 0; i < 10; i++) {
    float u = 1 - t, uu = u * u, tt = t * t;
    float bx = 3 * uu * t * p1x + 3 * u * tt * p2x + tt * t;
    float dbx = 3 * u * u * p1x + 6 * u * t * (p2x - p1x) + 3 * t * t * (1 - p2x);
    if (fabsf(dbx) < 1e-7f)
      break;
    t = clampf(t - (bx - x) / dbx, 0, 1);
  }
  float u = 1 - t, uu = u * u, tt = t * t;
  return 3 * uu * t * p1y + 3 * u * tt * p2y + tt * t;
}

// --- Bezier rendering ---

// Stroke a cubic bezier with current line width and colour.
static void render_stroke_bezier(const Bezier* b) {
  render_path_begin();
  render_path_move_to(b->x0, b->y0);
  render_path_cubic_to(b->cx1, b->cy1, b->cx2, b->cy2, b->x1, b->y1);
  render_path_stroke();
}

// Stroke a quadratic bezier.
static void render_stroke_quad_bezier(const QuadBez* b) {
  render_path_begin();
  render_path_move_to(b->x0, b->y0);
  render_path_quad_to(b->cx, b->cy, b->x1, b->y1);
  render_path_stroke();
}

// Fill the region between the curve and a horizontal baseline.
// Useful for area charts, graphs, and wave effects.
static void render_fill_bezier_area(const Bezier* b, float base_y) {
  render_path_begin();
  render_path_move_to(b->x0, base_y);
  render_path_line_to(b->x0, b->y0);
  render_path_cubic_to(b->cx1, b->cy1, b->cx2, b->cy2, b->x1, b->y1);
  render_path_line_to(b->x1, base_y);
  render_path_close();
  render_path_fill();
}

// ---------------------------------------------------------------------------
// Demo scene
// ---------------------------------------------------------------------------

static GtkWidget* g_gl = NULL;
static guint g_tick = 0;

// Thin arrow helper
static void draw_arrow(float x0, float y0, float x1, float y1, float hw) {
  render_draw_line(x0, y0, x1, y1);
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
  float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
  float bx = x1 - ux * hw * 2, by = y1 - uy * hw * 2;
  float pts[6] = {x1, y1, bx + nx * hw, by + ny * hw, bx - nx * hw, by - ny * hw};
  render_fill_polygon(pts, 3);
}

// Small square marker (unaffected by current transform)
static void draw_square(float cx, float cy, float r) {
  render_fill_rect(cx - r, cy - r, r * 2, r * 2);
}

static gboolean on_render(GtkGLArea* area, GdkGLContext* ctx, gpointer ud) {
  int w = gtk_widget_get_width(GTK_WIDGET(area));
  int h = gtk_widget_get_height(GTK_WIDGET(area));
  float t = ticker_time(&G_TICK);
  render_begin(w, h);

  // Background
  render_set_color(.09f, .10f, .13f, 1);
  render_fill_rect(0, 0, w, h);

  // Title
  render_set_font_size(20);
  const char* title = "Bezier Curves  ·  Platform-Independent Ticker";
  render_set_color(1, 1, 1, 1);
  render_draw_text((w - render_measure_text(title)) * .5f, 10, title);
  render_set_font_size(16);

  float gx = 16, gy = 44;          // grid origin
  float gw = (w - gx * 3) * .5f;   // panel width
  float gh = (h - gy - 48) * .5f;  // panel height

  // Panel borders
  render_set_color(.18f, .20f, .27f, 1);
  render_fill_rounded_rect(gx, gy, gw, gh, 8);
  render_fill_rounded_rect(gx * 2 + gw, gy, gw, gh, 8);
  render_fill_rounded_rect(gx, gy + gh + gx, gw, gh, 8);
  render_fill_rounded_rect(gx * 2 + gw, gy + gh + gx, gw, gh, 8);

  // ── Panel 1 (top-left): bezier_pt / tangent / normal ────────────────────
  {
    float px = gx + 16, py = gy + 10, pw = gw - 32, ph = gh - 20;
    // Bezier with slowly-wiggling control points
    float wox = 22 * sinf(t * .25f), woy = 18 * cosf(t * .31f);
    Bezier cv = {
        px,      py + ph * .55f, px + pw * .28f + wox, py + ph * .05f, px + pw * .72f - wox, py + ph * .95f + woy,
        px + pw, py + ph * .45f};

    // Control polygon
    render_set_color(.35f, .38f, .50f, .7f);
    render_set_line_width(1);
    render_draw_line(cv.x0, cv.y0, cv.cx1, cv.cy1);
    render_draw_line(cv.x1, cv.y1, cv.cx2, cv.cy2);
    render_draw_line(cv.cx1, cv.cy1, cv.cx2, cv.cy2);

    // Curve
    render_set_color(.40f, .70f, 1.f, 1);
    render_set_line_width(2.5f);
    render_stroke_bezier(&cv);

    // Endpoint markers
    render_set_color(.40f, .80f, 1.f, 1);
    render_fill_circle(cv.x0, cv.y0, 4);
    render_fill_circle(cv.x1, cv.y1, 4);

    // Control-point markers (orange)
    render_set_color(1.f, .60f, .15f, 1);
    draw_square(cv.cx1, cv.cy1, 5);
    draw_square(cv.cx2, cv.cy2, 5);

    // Dot travelling along the curve
    float tp = ease_smooth(ping_pong(t * .5f, 1));
    float dpx, dpy, dtx, dty, dnx, dny;
    bezier_pt(&cv, tp, &dpx, &dpy);
    bezier_tan(&cv, tp, &dtx, &dty);
    bezier_normal(&cv, tp, &dnx, &dny);
    float tlen = sqrtf(dtx * dtx + dty * dty) + 1e-6f;

    render_set_color(1.f, .85f, .20f, 1);
    render_set_line_width(1.5f);
    draw_arrow(dpx, dpy, dpx + dtx / tlen * 32, dpy + dty / tlen * 32, 4);

    render_set_color(.25f, .95f, .55f, 1);
    render_set_line_width(1.5f);
    draw_arrow(dpx, dpy, dpx + dnx * 24, dpy + dny * 24, 4);

    render_set_color(1, 1, 1, 1);
    render_fill_circle(dpx, dpy, 6);

    render_set_color(.55f, .70f, .90f, .75f);
    render_draw_text(px, py + ph + 2, "bezier_pt  \xc2\xb7  bezier_tan  \xc2\xb7  bezier_normal");
  }

  // ── Panel 2 (top-right): De Casteljau split visualisation ───────────────
  {
    float px = gx * 2 + gw + 16, py = gy + 14, pw = gw - 32, ph = gh - 28;
    Bezier cv = {px,      py + ph * .50f, px + pw * .20f, py + ph * .02f, px + pw * .80f, py + ph * .98f,
                 px + pw, py + ph * .50f};
    float sp = ping_pong(t * .4f, 1);  // split parameter animates 0→1→0

    float q0x, q0y, q1x, q1y, q2x, q2y, r0x, r0y, r1x, r1y;
    Bezier L, Rv;
    bezier_split_dbg(&cv, sp, &L, &Rv, &q0x, &q0y, &q1x, &q1y, &q2x, &q2y, &r0x, &r0y, &r1x, &r1y);
    float mx, my;
    bezier_pt(&cv, sp, &mx, &my);

    // Control polygon (faint)
    render_set_color(.30f, .32f, .44f, .6f);
    render_set_line_width(1);
    render_draw_line(cv.x0, cv.y0, cv.cx1, cv.cy1);
    render_draw_line(cv.cx1, cv.cy1, cv.cx2, cv.cy2);
    render_draw_line(cv.cx2, cv.cy2, cv.x1, cv.y1);

    // Left half — blue
    render_set_color(.30f, .65f, 1.f, .9f);
    render_set_line_width(3);
    render_stroke_bezier(&L);
    // Right half — salmon
    render_set_color(1.f, .45f, .45f, .9f);
    render_stroke_bezier(&Rv);

    // Level-1 interpolation lines (q0-q1-q2)
    render_set_color(.80f, .75f, .30f, .55f);
    render_set_line_width(1);
    render_draw_line(q0x, q0y, q1x, q1y);
    render_draw_line(q1x, q1y, q2x, q2y);

    // Level-2 interpolation line (r0-r1)
    render_set_color(.55f, .90f, .55f, .55f);
    render_draw_line(r0x, r0y, r1x, r1y);

    // Construction dots
    render_set_color(.80f, .75f, .30f, 1);
    render_fill_circle(q0x, q0y, 4);
    render_fill_circle(q1x, q1y, 4);
    render_fill_circle(q2x, q2y, 4);
    render_set_color(.55f, .90f, .55f, 1);
    render_fill_circle(r0x, r0y, 4);
    render_fill_circle(r1x, r1y, 4);

    // Split point
    render_set_color(1, 1, 1, 1);
    render_fill_circle(mx, my, 6);
    render_set_color(.25f, .30f, .40f, 1);
    render_fill_circle(mx, my, 3);

    char buf[24];
    snprintf(buf, sizeof buf, "t = %.2f", sp);
    render_set_color(.55f, .70f, .90f, .75f);
    render_draw_text(px, py + ph + 4, "bezier_split  \xc2\xb7  de Casteljau");
    render_set_color(.85f, .85f, .65f, .85f);
    render_draw_text(px + pw - render_measure_text(buf) - 2, py + ph + 4, buf);
  }

  // ── Panel 3 (bottom-left): CSS easing curves with bezier_ease ───────────
  {
    float px = gx + 16, py = gy + gh + gx + 12, pw = gw - 32, ph = gh - 26;

    // Three named easing functions (CSS cubic-bezier params)
    static const struct {
      float p1x, p1y, p2x, p2y;
      const char* name;
      float r, g, b;
    } easings[] = {
        {0.25f, 0.10f, 0.25f, 1.00f, "ease", .40f, .80f, 1.f},
        {0.42f, 0.00f, 0.58f, 1.00f, "ease-in-out", 1.f, .65f, .25f},
        {0.68f, -.55f, 0.27f, 1.55f, "back-in-out", .60f, 1.f, .55f},
    };
    int ne = 3;
    float ball_period = 2.4f;

    // Plot each easing curve
    for (int e = 0; e < ne; e++) {
      float cr = easings[e].r, cg = easings[e].g, cb = easings[e].b;
      render_set_color(cr, cg, cb, .35f);
      render_set_line_width(1.5f);
      // Draw curve as a polyline through the panel
      render_path_begin();
      for (int i = 0; i <= 64; i++) {
        float xi = (float)i / 64;
        float yi = bezier_ease(easings[e].p1x, easings[e].p1y, easings[e].p2x, easings[e].p2y, xi);
        float sx = px + xi * pw, sy = py + ph - (yi * ph);
        if (i == 0)
          render_path_move_to(sx, sy);
        else
          render_path_line_to(sx, sy);
      }
      render_set_color(cr, cg, cb, .80f);
      render_set_line_width(2);
      render_path_stroke();

      // Animated ball using this easing
      float raw = ping_pong(t, ball_period);
      float eased = bezier_ease(easings[e].p1x, easings[e].p1y, easings[e].p2x, easings[e].p2y, raw);
      float bx2 = px + eased * pw;
      float by2 = py + ph * .18f + e * (ph * .27f);
      render_set_color(cr, cg, cb, 1);
      render_fill_circle(bx2, by2, 9);
      render_set_color(0, 0, 0, .5f);
      render_fill_circle(bx2, by2, 4);

      // Label
      render_set_color(cr, cg, cb, .85f);
      render_draw_text(px, py + ph * .18f + e * (ph * .27f) - 9, easings[e].name);
    }

    render_set_color(.55f, .70f, .90f, .75f);
    render_draw_text(px, py + ph + 4, "bezier_ease  \xc2\xb7  CSS timing curves");
  }

  // ── Panel 4 (bottom-right): bezier area fill — animated wave chart ──────
  {
    float px = gx * 2 + gw + 16, py = gy + gh + gx + 12, pw = gw - 32, ph = gh - 26;
    float base = py + ph;

    // Three layered waves
    static const struct {
      float r, g, b, a;
      float speed, amp, phase;
    } waves[] = {
        {.25f, .55f, 1.f, .45f, .7f, .38f, 0.f},
        {.55f, .30f, 1.f, .40f, .45f, .28f, 1.2f},
        {.25f, .80f, .60f, .35f, .55f, .22f, 2.4f},
    };
    for (int wi = 2; wi >= 0; wi--) {
      float sp = waves[wi].speed, amp = waves[wi].amp * ph, ph2 = waves[wi].phase;
      float y0 = base - ph * (.45f + amp * .5f * sinf(t * sp + ph2));
      float y1 = base - ph * (.45f + amp * .5f * sinf(t * sp + ph2 + 0.8f));
      Bezier wave = {px,
                     y0,
                     px + pw * .33f,
                     base - ph * (.55f + amp * sinf(t * sp + ph2 + 0.5f)),
                     px + pw * .66f,
                     base - ph * (.35f + amp * sinf(t * sp + ph2 + 1.5f)),
                     px + pw,
                     y1};
      render_set_color(waves[wi].r, waves[wi].g, waves[wi].b, waves[wi].a);
      render_fill_bezier_area(&wave, base);
      render_set_color(waves[wi].r, waves[wi].g, waves[wi].b, waves[wi].a + .25f);
      render_set_line_width(1.5f);
      render_stroke_bezier(&wave);
    }

    render_set_color(.55f, .70f, .90f, .75f);
    render_draw_text(px, py + ph + 4, "render_fill_bezier_area  \xc2\xb7  render_stroke_bezier");
  }

  render_end();
  return TRUE;
  (void)ctx;
  (void)ud;
}

// ── Background ──────────────────────────────────────────────────────
render_set_color(.10f, .11f, .14f, 1);
render_fill_rect(0, 0, w, h);

// ── 1. Animated snowflake (push/pop + rotate) ────────────────────────
float sfx = w * .20f, sfy = h * .28f;
render_push();
render_translate(sfx, sfy);
for (int i = 0; i < 6; i++) {
  render_push();
  render_rotate((float)i * (float)M_PI / 3.f + t * .4f);
  render_set_color(.55f, .80f, 1.0f, .9f);
  render_fill_rounded_rect(-5.f, -8.f, 10.f, 58.f, 5.f);
  // diamond at tip
  render_push();
  render_translate(0, 58);
  render_rotate((float)M_PI * .25f);
  render_set_color(.85f, .95f, 1.f, 1.f);
  render_fill_rect(-6, -6, 12, 12);
  render_pop();
  render_pop();
}
// pulsing center circle
float pulse = lerp_f(10, 17, ease_smooth(ping_pong(t, 1.2f)));
render_set_color(1.f, 1.f, 1.f, 1.f);
render_fill_circle(0, 0, pulse);
render_set_color(.55f, .80f, 1.f, 1.f);
render_draw_circle(0, 0, pulse + 2);
render_pop();
render_set_color(.75f, .88f, 1.f, .8f);
const char* sfl = "Transforms";
render_draw_text(sfx - render_measure_text(sfl) * .5f, sfy + 82, sfl);

// ── 2. Bezier path — animated wave blob ──────────────────────────────
float bpx = w * .50f, bpy = h * .25f;
float phase = t * .6f;
float ox = 55 * cosf(phase), oy = 20 * sinf(phase * 1.3f);
render_set_color(.30f, .55f, 1.f, .55f);
render_push();
render_translate(bpx, bpy);
render_path_begin();
render_path_move_to(-70 + ox, -10 + oy);
render_path_cubic_to(-70, -80, 70, -80, 70 + ox, -10 + oy);
render_path_cubic_to(70, 60, -70, 60, -70 + ox, -10 + oy);
render_path_close();
render_path_fill();
render_set_color(.70f, .85f, 1.f, .90f);
render_set_line_width(2.5f);
render_path_begin();
render_path_move_to(-70 + ox, -10 + oy);
render_path_cubic_to(-70, -80, 70, -80, 70 + ox, -10 + oy);
render_path_cubic_to(70, 60, -70, 60, -70 + ox, -10 + oy);
render_path_close();
render_path_stroke();
render_pop();
render_set_color(.75f, .88f, 1.f, .8f);
const char* bpl = "Bezier Paths";
render_draw_text(bpx - render_measure_text(bpl) * .5f, bpy + 90, bpl);

// ── 3. Ellipse + rotating indicator ──────────────────────────────────
float epx = w * .80f, epy = h * .25f;
render_set_color(.80f, .40f, 1.f, .40f);
render_fill_ellipse(epx, epy, 65, 35);
render_set_color(.90f, .60f, 1.f, .90f);
render_draw_circle(epx, epy, 35);
render_push();
render_translate(epx, epy);
render_rotate(t * 1.2f);
render_set_color(1.f, .85f, .30f, 1.f);
render_fill_circle(35, 0, 7);
render_set_color(.60f, .30f, 1.f, .8f);
render_draw_line(0, 0, 35, 0);
render_pop();
render_set_color(.75f, .88f, 1.f, .8f);
const char* el = "Ellipse";
render_draw_text(epx - render_measure_text(el) * .5f, epy + 50, el);

// ── 4. Star polygon (ear-clipped fill) ───────────────────────────────
float stx = w * .20f, sty = h * .68f;
float star[10 * 2];
int sp = 5;
float r1 = 48, r2 = 22;
for (int i = 0; i < sp * 2; i++) {
  float a = (float)i * M_PI / sp - M_PI * .5f + t * .5f;
  float rv = (i % 2 == 0) ? r1 : r2;
  star[i * 2] = rv * cosf(a);
  star[i * 2 + 1] = rv * sinf(a);
}
render_push();
render_translate(stx, sty);
render_set_color(1.f, .80f, .20f, .85f);
render_fill_polygon(star, sp * 2);
render_set_color(1.f, .95f, .50f, 1.f);
render_draw_polygon(star, sp * 2);
render_pop();
render_set_color(.75f, .88f, 1.f, .8f);
const char* sl = "Polygon (ear-clip)";
render_draw_text(stx - render_measure_text(sl) * .5f, sty + 62, sl);

// ── 5. Rounded-rect card + arc path ──────────────────────────────────
float cx2 = w * .52f, cy2 = h * .65f, cw = 170, ch = 110;
render_set_color(.14f, .18f, .26f, 1.f);
render_fill_rounded_rect(cx2 - cw * .5f, cy2 - ch * .5f, cw, ch, 14);
render_set_color(.30f, .45f, .70f, .8f);
render_draw_rounded_rect(cx2 - cw * .5f, cy2 - ch * .5f, cw, ch, 14);

// Arc progress ring inside the card
float prog = ease_smooth(ping_pong(t * .25f, 1.f));
render_push();
render_translate(cx2, cy2);
render_set_color(.18f, .24f, .35f, 1.f);
render_fill_circle(0, 0, 36);
render_set_color(.25f, .55f, .95f, 1.f);
render_set_line_width(6.f);
render_path_begin();
render_path_arc(0, 0, 30, -M_PI * .5f, -M_PI * .5f + 2 * (float)M_PI * prog);
render_path_stroke();
render_pop();
char pctbuf[8];
snprintf(pctbuf, sizeof(pctbuf), "%d%%", (int)(prog * 100));
render_set_color(1.f, 1.f, 1.f, .95f);
render_draw_text(cx2 - render_measure_text(pctbuf) * .5f, cy2 - 9, pctbuf);
render_set_color(.65f, .75f, .90f, .8f);
const char* cl = "Arc + Rounded Rect";
render_draw_text(cx2 - render_measure_text(cl) * .5f, cy2 + ch * .5f + 6, cl);

// ── 6. Bouncing ball with squash/stretch ─────────────────────────────
float bx = w * .80f;
float bt = ping_pong(t, 1.4f);
float bh2 = h * .85f - 60, bl = h * .85f;
float by2 = lerp_f(bl, bh2, ease_out(bt));
float sq = lerp_f(1.f, .65f, ease_out(clampf(1.f - bt * 4, 0, 1)));   // squash on land
float sc2 = lerp_f(1.f, 1.4f, ease_out(clampf(1.f - bt * 4, 0, 1)));  // stretch wide
render_push();
render_translate(bx, by2);
render_scale(sc2, sq);
render_set_color(1.f, .35f, .35f, 1.f);
render_fill_circle(0, 0, 18);
render_set_color(1.f, .55f, .55f, .7f);
render_draw_circle(0, 0, 18);
render_pop();
// Shadow on floor
float shsc = lerp_f(.4f, 1.f, 1.f - ease_out(bt));
render_push();
render_translate(bx, h * .85f + 4);
render_scale(shsc, 1);
render_set_color(0, 0, 0, .35f);
render_fill_ellipse(0, 0, 20, 5);
render_pop();
render_set_color(.75f, .88f, 1.f, .8f);
const char* bb = "Squash & Stretch";
render_draw_text(bx - render_measure_text(bb) * .5f, h * .85f + 20, bb);

// ── Title ────────────────────────────────────────────────────────────
render_set_font_size(22);
const char* title = "GTK4 · OpenGL · Path · Transform · Animation";
render_set_color(1.f, 1.f, 1.f, 1.f);
render_draw_text((w - render_measure_text(title)) * .5f, 12, title);
render_set_font_size(18);

render_end();
return TRUE;
}

// ---------------------------------------------------------------------------
// GTK4 plumbing
// ---------------------------------------------------------------------------

static void on_realize(GtkGLArea* a, gpointer ud) {
  gtk_gl_area_make_current(a);
  if (gtk_gl_area_get_error(a))
    return;
  ren_init();
  anim_start(GTK_WIDGET(a), on_update, NULL);
}

static void on_unrealize(GtkGLArea* a, gpointer ud) {
  gtk_gl_area_make_current(a);
  anim_stop(GTK_WIDGET(a));
  ren_free();
}

static void on_activate(GtkApplication* app, gpointer ud) {
  GtkWidget* win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "GTK4 + OpenGL + Paths + Animation");
  gtk_window_set_default_size(GTK_WINDOW(win), 960, 600);
  g_gl = gtk_gl_area_new();
  gtk_gl_area_set_required_version(GTK_GL_AREA(g_gl), 3, 3);
  g_signal_connect(g_gl, "realize", G_CALLBACK(on_realize), NULL);
  g_signal_connect(g_gl, "unrealize", G_CALLBACK(on_unrealize), NULL);
  g_signal_connect(g_gl, "render", G_CALLBACK(on_render), NULL);
  gtk_window_set_child(GTK_WINDOW(win), g_gl);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char* argv[]) {
  GtkApplication* app = gtk_application_new("com.example.gl_paths", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int s = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return s;
}
