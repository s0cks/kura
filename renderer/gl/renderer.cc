#include "renderer/gl/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <epoxy/gl.h>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "text/text.h"

#ifndef KURA_MAX_NUMBER_OF_VERTICES
#define KURA_MAX_NUMBER_OF_VERTICES 16384
#endif  // KURA_MAX_NUMBER_OF_VERTICES

#ifndef KURA_MAX_NUMBER_OF_INDICES
#define KURA_MAX_NUMBER_OF_INDICES 32768
#endif  // KURA_MAX_NUMBER_OF_INDICES

namespace kura::renderer {
namespace {
using Status = KuraRendererStatus;

constexpr size_t kMaxVertices = KURA_MAX_NUMBER_OF_VERTICES;
constexpr size_t kMaxIndices = KURA_MAX_NUMBER_OF_INDICES;

// Coordinates are clamped to this before they reach the GPU, so that a rect at
// 1e30 can't turn into inf or nan in the shader. It's far larger than any surface.
constexpr float kCoordLimit = 1 << 20;

constexpr const char* kVertexShader =
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

constexpr const char* kFragmentShader =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "out vec4 out_color;\n"
    "void main() { out_color = v_color; }\n";

// Text is drawn as textured quads: the glyph's coverage is in the atlas's red channel, and tints the color.
constexpr const char* kTextVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform vec2 u_res;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "  vec2 p = a_pos / u_res * 2.0 - 1.0;\n"
    "  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "  v_color = a_color;\n"
    "}\n";

constexpr const char* kTextFragmentShader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_atlas;\n"
    "out vec4 out_color;\n"
    "void main() { out_color = vec4(v_color.rgb, v_color.a * texture(u_atlas, v_uv).r); }\n";

// The side of a font's glyph atlas, in texels. One texture holds a family's glyphs at every size drawn; when
// it fills up it's emptied and filled again, which only costs rasterizing what's drawn next.
constexpr int kAtlasSize = 1024;

// The same checks the headless renderer makes, so that renderers agree on what is an error.
inline auto IsValid(const float value) -> bool {
  return std::isfinite(value);
}

inline auto IsValid(const KuraVec2& value) -> bool {
  return IsValid(value.x) && IsValid(value.y);
}

inline auto IsValid(const KuraRect& value) -> bool {
  return IsValid(value.x) && IsValid(value.y) && IsValid(value.width) && IsValid(value.height) && value.width >= 0.0f &&
         value.height >= 0.0f;
}

inline auto IsValid(const KuraColor& value) -> bool {
  return IsValid(value.r) && IsValid(value.g) && IsValid(value.b) && IsValid(value.a);
}

inline auto IsValid(const KuraTextStyle& value) -> bool {
  return IsValid(value.size) && value.size > 0.0f && (value.family != nullptr || value.family_length == 0);
}

inline auto FamilyOf(const KuraTextStyle& style) -> std::string {
  return style.family == nullptr || style.family_length == 0 ? std::string{}
                                                              : std::string(style.family, style.family_length);
}

inline auto Coord(const float value) -> float {
  return std::clamp(value, -kCoordLimit, kCoordLimit);
}

inline auto Channel(const float value) -> float {
  return std::clamp(value, 0.0f, 1.0f);
}

struct Vertex {
  float x;
  float y;
  float r;
  float g;
  float b;
  float a;
};

struct TextVertex {
  float x;
  float y;
  float u;
  float v;
  float r;
  float g;
  float b;
  float a;
};

// Where a glyph is in its atlas, and where it sits from the pen.
struct GlyphEntry {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
  float u0 = 0;
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
  // Nothing to draw: a space, or a glyph too big for the atlas.
  bool empty = false;
};

// One font's glyphs: a texture packed in rows (a shelf at a time), and where each glyph went.
struct Atlas {
  GLuint texture = 0;
  int cursor_x = 0;
  int cursor_y = 0;
  int row_height = 0;
  std::unordered_map<uint64_t, GlyphEntry> glyphs{};
};

// A half-open rect of pixels: [x0, x1) x [y0, y1), with (0, 0) at the top-left.
struct PixelRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;

  inline auto Intersect(const PixelRect& rhs) const -> PixelRect {
    PixelRect out{std::max(x0, rhs.x0), std::max(y0, rhs.y0), std::min(x1, rhs.x1), std::min(y1, rhs.y1)};
    out.x1 = std::max(out.x0, out.x1);
    out.y1 = std::max(out.y0, out.y1);
    return out;
  }
};

// The GL state a frame changes, so `end_frame` can put it back.
struct SavedState {
  GLint viewport[4];
  GLint scissor_box[4];
  GLint program;
  GLint vao;
  GLint array_buffer;
  GLint blend_func[4];
  GLint active_texture;
  GLint texture_2d;
  GLboolean blend;
  GLboolean scissor_test;
  GLboolean depth_test;
  GLboolean cull_face;
};

auto Compile(const GLenum kind, const char* source) -> GLuint {
  const auto shader = glCreateShader(kind);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
    glDeleteShader(shader);
    throw std::runtime_error(std::string("kura gl renderer: shader failed to compile: ") + log);
  }
  return shader;
}
}  // namespace

class GlImpl {
 public:
  explicit GlImpl(const KuraGlOptions& options) :
    clear_on_begin_(options.clear_on_begin != 0),
    clear_(options.clear) {
    GLint array_buffer = 0;
    GLint vao = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);

    try {
      CreateProgram();
      CreateBuffers();
    } catch (...) {
      Destroy();
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
      throw;
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, array_buffer);

    verts_.reserve(kMaxVertices);
    idx_.reserve(kMaxIndices);
  }

  ~GlImpl() {
    Destroy();
  }

  GlImpl(const GlImpl&) = delete;
  GlImpl& operator=(const GlImpl&) = delete;

  auto BeginFrame(const uint32_t width, const uint32_t height) -> Status {
    if (in_frame_)
      return KURA_RENDERER_ERR_IN_FRAME;
    if (width > KURA_RENDERER_MAX_SURFACE_SIZE || height > KURA_RENDERER_MAX_SURFACE_SIZE)
      return KURA_RENDERER_ERR_INVALID_ARG;

    Save();
    width_ = width;
    height_ = height;
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    if (clear_on_begin_) {
      glClearColor(Channel(clear_.r), Channel(clear_.g), Channel(clear_.b), Channel(clear_.a));
      glClear(GL_COLOR_BUFFER_BIT);
    }
    // source-over on non-premultiplied colors
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(u_res_, static_cast<float>(width), static_cast<float>(height));

    verts_.clear();
    idx_.clear();
    clips_.clear();
    in_frame_ = true;
    return KURA_RENDERER_OK;
  }

  auto EndFrame() -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    Flush();
    in_frame_ = false;
    const auto balanced = clips_.empty();
    clips_.clear();
    Restore();
    return balanced ? KURA_RENDERER_OK : KURA_RENDERER_ERR_CLIP_UNBALANCED;
  }

  auto PushClip(const KuraRect& rect) -> Status {
    if (const auto status = Check(rect); status != KURA_RENDERER_OK)
      return status;
    clips_.push_back(CurrentClip().Intersect(CoveredPixels(rect)));
    ApplyClip();
    return KURA_RENDERER_OK;
  }

  auto PopClip() -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (clips_.empty())
      return KURA_RENDERER_ERR_CLIP_UNDERFLOW;
    clips_.pop_back();
    ApplyClip();
    return KURA_RENDERER_OK;
  }

  auto FillRect(const KuraRect& rect, const KuraColor& color) -> Status {
    if (const auto status = Check(rect, color); status != KURA_RENDERER_OK)
      return status;
    AddRect(rect.x, rect.y, rect.width, rect.height, color);
    return KURA_RENDERER_OK;
  }

  auto StrokeRect(const KuraRect& rect, const KuraColor& color) -> Status {
    if (const auto status = Check(rect, color); status != KURA_RENDERER_OK)
      return status;
    if (rect.width <= 0.0f || rect.height <= 0.0f)
      return KURA_RENDERER_OK;

    // the ring is four rects that don't overlap, so a translucent outline is even
    const auto tx = std::min(1.0f, rect.width);
    const auto ty = std::min(1.0f, rect.height);
    AddRect(rect.x, rect.y, rect.width, ty, color);
    if (rect.height > ty)
      AddRect(rect.x, rect.y + rect.height - ty, rect.width, ty, color);
    if (rect.height > 2.0f * ty) {
      const auto inner = rect.height - 2.0f * ty;
      AddRect(rect.x, rect.y + ty, tx, inner, color);
      if (rect.width > tx)
        AddRect(rect.x + rect.width - tx, rect.y + ty, tx, inner, color);
    }
    return KURA_RENDERER_OK;
  }

  auto DrawLine(const KuraVec2& from, const KuraVec2& to, const KuraColor& color) -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (!IsValid(from) || !IsValid(to) || !IsValid(color))
      return KURA_RENDERER_ERR_INVALID_ARG;

    // a line is a quad one pixel wide, so it's batched with everything else
    const auto dx = static_cast<double>(to.x) - static_cast<double>(from.x);
    const auto dy = static_cast<double>(to.y) - static_cast<double>(from.y);
    const auto length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6) {
      AddRect(from.x - 0.5f, from.y - 0.5f, 1.0f, 1.0f, color);
      return KURA_RENDERER_OK;
    }

    const auto nx = static_cast<float>(-dy / length * 0.5);
    const auto ny = static_cast<float>(dx / length * 0.5);
    AddQuad(Make(from.x + nx, from.y + ny, color), Make(from.x - nx, from.y - ny, color),
            Make(to.x - nx, to.y - ny, color), Make(to.x + nx, to.y + ny, color));
    return KURA_RENDERER_OK;
  }

  auto DrawText(const KuraVec2& pos, const char* utf8, const size_t length, const KuraTextStyle& style,
                const KuraColor& color) -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (!IsValid(pos) || !IsValid(style) || !IsValid(color) || (utf8 == nullptr && length != 0))
      return KURA_RENDERER_ERR_INVALID_ARG;
    const auto font = fonts_.Find(FamilyOf(style));
    if (font == nullptr)
      return KURA_RENDERER_OK;
    const auto lines = font->Shape(std::string_view(length == 0 ? "" : utf8, length), style.size);
    const auto metrics = font->GetMetrics(style.size);

    // what was batched is drawn first, so that text lands over it and under what comes later
    Flush();
    EnsureTextPipeline();
    auto& atlas = AtlasFor(font);
    const auto r = Channel(color.r);
    const auto g = Channel(color.g);
    const auto b = Channel(color.b);
    const auto a = Channel(color.a);
    const auto target_height = static_cast<double>(pos.y) + static_cast<double>(metrics.ascent);

    auto baseline = target_height;
    for (const auto& line : lines) {
      auto pen = static_cast<double>(pos.x);
      for (const auto& glyph : line.glyphs) {
        const auto entry = GlyphFor(atlas, font, glyph.id, style.size);
        if (entry != nullptr && !entry->empty) {
          const auto left = static_cast<float>(std::round(pen + static_cast<double>(glyph.x_offset))) +
                            static_cast<float>(entry->left);
          const auto top = static_cast<float>(std::round(baseline + static_cast<double>(glyph.y_offset))) -
                           static_cast<float>(entry->top);
          const auto right = left + static_cast<float>(entry->width);
          const auto bottom = top + static_cast<float>(entry->height);
          AddGlyphQuad(atlas, TextVertex{Coord(left), Coord(top), entry->u0, entry->v0, r, g, b, a},
                       TextVertex{Coord(right), Coord(top), entry->u1, entry->v0, r, g, b, a},
                       TextVertex{Coord(right), Coord(bottom), entry->u1, entry->v1, r, g, b, a},
                       TextVertex{Coord(left), Coord(bottom), entry->u0, entry->v1, r, g, b, a});
        }
        pen += static_cast<double>(glyph.x_advance);
      }
      baseline += static_cast<double>(metrics.line_height);
    }
    FlushText(atlas);
    return KURA_RENDERER_OK;
  }

  // The size of text in the font the family names: the same shaping the text is drawn with, so what layout
  // measures is what gets drawn. Without any font at all, the fixed model keeps layout working.
  auto MeasureText(const char* utf8, const size_t length, const KuraTextStyle& style, KuraTextMetrics* out) -> Status {
    if (out == nullptr || !IsValid(style) || (utf8 == nullptr && length != 0))
      return KURA_RENDERER_ERR_INVALID_ARG;
    if (const auto font = fonts_.Find(FamilyOf(style))) {
      const auto metrics = font->Measure(std::string_view(length == 0 ? "" : utf8, length), style.size);
      out->width = metrics.width;
      out->height = metrics.height;
      out->ascent = metrics.ascent;
      return KURA_RENDERER_OK;
    }
    return FixedMetrics(utf8, length, style, out);
  }

  // The fixed-width model the headless renderer uses: every code point is 0.5 * size wide, a line is
  // 1.25 * size tall, and the baseline is `size` below the top of the first line.
  static auto FixedMetrics(const char* utf8, const size_t length, const KuraTextStyle& style, KuraTextMetrics* out)
      -> Status {
    if (out == nullptr || !IsValid(style) || (utf8 == nullptr && length != 0))
      return KURA_RENDERER_ERR_INVALID_ARG;
    size_t lines = 1;
    size_t current = 0;
    size_t widest = 0;
    for (size_t idx = 0; idx < length; idx++) {
      const auto byte = static_cast<unsigned char>(utf8[idx]);
      if (byte == '\n') {
        widest = std::max(widest, current);
        current = 0;
        lines++;
      } else if ((byte & 0xC0) != 0x80) {  // not a continuation byte: a new code point
        current++;
      }
    }
    widest = std::max(widest, current);
    out->width = static_cast<float>(widest) * style.size * 0.5f;
    out->height = static_cast<float>(lines) * style.size * 1.25f;
    out->ascent = style.size;
    return KURA_RENDERER_OK;
  }

 private:
  bool clear_on_begin_;
  KuraColor clear_;

  GLuint program_ = 0;
  GLint u_res_ = -1;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ibo_ = 0;

  bool in_frame_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  std::vector<PixelRect> clips_{};
  std::vector<Vertex> verts_{};
  std::vector<uint32_t> idx_{};
  SavedState saved_{};

  // Text. The pipeline and the atlases are made the first time text is drawn, so a renderer that never draws
  // any never touches them. The library finds the font of a family; an atlas is kept per font, so families
  // that fall back to the same font share one.
  text::FontLibrary fonts_{};
  GLuint text_program_ = 0;
  GLint text_u_res_ = -1;
  GLuint text_vao_ = 0;
  GLuint text_vbo_ = 0;
  GLuint text_ibo_ = 0;
  std::unordered_map<const text::Font*, Atlas> atlases_{};
  std::vector<TextVertex> text_verts_{};
  std::vector<uint32_t> text_idx_{};

  void EnsureTextPipeline() {
    if (text_program_ != 0)
      return;
    const auto vs = Compile(GL_VERTEX_SHADER, kTextVertexShader);
    GLuint fs = 0;
    try {
      fs = Compile(GL_FRAGMENT_SHADER, kTextFragmentShader);
    } catch (...) {
      glDeleteShader(vs);
      throw;
    }
    const auto program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
      char log[1024] = {};
      glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
      glDeleteProgram(program);
      throw std::runtime_error(std::string("kura gl renderer: text program failed to link: ") + log);
    }
    text_program_ = program;
    text_u_res_ = glGetUniformLocation(text_program_, "u_res");
    glUseProgram(text_program_);
    glUniform1i(glGetUniformLocation(text_program_, "u_atlas"), 0);

    glGenVertexArrays(1, &text_vao_);
    glGenBuffers(1, &text_vbo_);
    glGenBuffers(1, &text_ibo_);
    glBindVertexArray(text_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, text_ibo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(offsetof(TextVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(offsetof(TextVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), reinterpret_cast<void*>(offsetof(TextVertex, r)));
    glBindVertexArray(0);
    text_verts_.reserve(kMaxVertices);
    text_idx_.reserve(kMaxIndices);
  }

  auto AtlasFor(const text::Font* font) -> Atlas& {
    auto& atlas = atlases_[font];
    if (atlas.texture == 0) {
      glGenTextures(1, &atlas.texture);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, atlas.texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return atlas;
  }

  // The glyph in the atlas, rasterized and uploaded the first time it's asked for. Null only if the atlas
  // couldn't take it, which can't happen to a glyph that fits.
  auto GlyphFor(Atlas& atlas, text::Font* font, const uint32_t id, const float size) -> const GlyphEntry* {
    const auto key = (static_cast<uint64_t>(id) << 32) | static_cast<uint32_t>(std::lround(size * 64.0f));
    if (const auto it = atlas.glyphs.find(key); it != atlas.glyphs.end())
      return &it->second;

    const auto bitmap = font->Rasterize(id, size);
    GlyphEntry entry{};
    // one texel of room around each glyph, so nothing bleeds into its neighbor
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.width + 1 > kAtlasSize || bitmap.height + 1 > kAtlasSize) {
      entry.empty = true;
      return &atlas.glyphs.emplace(key, entry).first->second;
    }

    if (atlas.cursor_x + bitmap.width + 1 > kAtlasSize) {
      atlas.cursor_x = 0;
      atlas.cursor_y += atlas.row_height + 1;
      atlas.row_height = 0;
    }
    if (atlas.cursor_y + bitmap.height + 1 > kAtlasSize) {
      // Full. What's queued refers to the glyphs about to go, so it's drawn before they do.
      FlushText(atlas);
      atlas.glyphs.clear();
      atlas.cursor_x = atlas.cursor_y = atlas.row_height = 0;
    }

    GLint alignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, atlas.cursor_x, atlas.cursor_y, bitmap.width, bitmap.height, GL_RED,
                    GL_UNSIGNED_BYTE, bitmap.alpha.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

    entry.left = bitmap.left;
    entry.top = bitmap.top;
    entry.width = bitmap.width;
    entry.height = bitmap.height;
    constexpr auto kTexel = 1.0f / static_cast<float>(kAtlasSize);
    entry.u0 = static_cast<float>(atlas.cursor_x) * kTexel;
    entry.v0 = static_cast<float>(atlas.cursor_y) * kTexel;
    entry.u1 = static_cast<float>(atlas.cursor_x + bitmap.width) * kTexel;
    entry.v1 = static_cast<float>(atlas.cursor_y + bitmap.height) * kTexel;
    atlas.cursor_x += bitmap.width + 1;
    atlas.row_height = std::max(atlas.row_height, bitmap.height);
    return &atlas.glyphs.emplace(key, entry).first->second;
  }

  void AddGlyphQuad(Atlas& atlas, const TextVertex& a, const TextVertex& b, const TextVertex& c, const TextVertex& d) {
    if (text_verts_.size() + 4 > kMaxVertices || text_idx_.size() + 6 > kMaxIndices)
      FlushText(atlas);
    const auto base = static_cast<uint32_t>(text_verts_.size());
    text_verts_.insert(text_verts_.end(), {a, b, c, d});
    text_idx_.insert(text_idx_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  // Draws the queued glyphs, which are all in `atlas`, under the clip that's current.
  void FlushText(const Atlas& atlas) {
    if (text_idx_.empty())
      return;
    glUseProgram(text_program_);
    glUniform2f(text_u_res_, static_cast<float>(width_), static_cast<float>(height_));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.texture);
    glBindVertexArray(text_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(text_verts_.size() * sizeof(TextVertex)), text_verts_.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, text_ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(text_idx_.size() * sizeof(uint32_t)),
                 text_idx_.data(), GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(text_idx_.size()), GL_UNSIGNED_INT, nullptr);
    text_verts_.clear();
    text_idx_.clear();
  }

  void CreateProgram() {
    const auto vs = Compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = 0;
    try {
      fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    } catch (...) {
      glDeleteShader(vs);
      throw;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
      char log[1024] = {};
      glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
      throw std::runtime_error(std::string("kura gl renderer: program failed to link: ") + log);
    }
    u_res_ = glGetUniformLocation(program_, "u_res");
  }

  void CreateBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);
  }

  void Destroy() {
    if (vao_ != 0)
      glDeleteVertexArrays(1, &vao_);
    if (vbo_ != 0)
      glDeleteBuffers(1, &vbo_);
    if (ibo_ != 0)
      glDeleteBuffers(1, &ibo_);
    if (program_ != 0)
      glDeleteProgram(program_);
    vao_ = vbo_ = ibo_ = program_ = 0;

    for (auto& [font, atlas] : atlases_) {
      if (atlas.texture != 0)
        glDeleteTextures(1, &atlas.texture);
    }
    atlases_.clear();
    if (text_vao_ != 0)
      glDeleteVertexArrays(1, &text_vao_);
    if (text_vbo_ != 0)
      glDeleteBuffers(1, &text_vbo_);
    if (text_ibo_ != 0)
      glDeleteBuffers(1, &text_ibo_);
    if (text_program_ != 0)
      glDeleteProgram(text_program_);
    text_vao_ = text_vbo_ = text_ibo_ = text_program_ = 0;
  }

  void Save() {
    glGetIntegerv(GL_VIEWPORT, saved_.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, saved_.scissor_box);
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_.array_buffer);
    glGetIntegerv(GL_BLEND_SRC_RGB, &saved_.blend_func[0]);
    glGetIntegerv(GL_BLEND_DST_RGB, &saved_.blend_func[1]);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved_.blend_func[2]);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &saved_.blend_func[3]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_.active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_.texture_2d);
    glActiveTexture(static_cast<GLenum>(saved_.active_texture));
    saved_.blend = glIsEnabled(GL_BLEND);
    saved_.scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    saved_.depth_test = glIsEnabled(GL_DEPTH_TEST);
    saved_.cull_face = glIsEnabled(GL_CULL_FACE);
  }

  void Restore() {
    const auto set = [](const GLenum cap, const GLboolean on) {
      if (on)
        glEnable(cap);
      else
        glDisable(cap);
    };
    set(GL_BLEND, saved_.blend);
    set(GL_SCISSOR_TEST, saved_.scissor_test);
    set(GL_DEPTH_TEST, saved_.depth_test);
    set(GL_CULL_FACE, saved_.cull_face);
    glBlendFuncSeparate(saved_.blend_func[0], saved_.blend_func[1], saved_.blend_func[2], saved_.blend_func[3]);
    glScissor(saved_.scissor_box[0], saved_.scissor_box[1], saved_.scissor_box[2], saved_.scissor_box[3]);
    glViewport(saved_.viewport[0], saved_.viewport[1], saved_.viewport[2], saved_.viewport[3]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(saved_.texture_2d));
    glActiveTexture(static_cast<GLenum>(saved_.active_texture));
    glBindBuffer(GL_ARRAY_BUFFER, saved_.array_buffer);
    glBindVertexArray(saved_.vao);
    glUseProgram(saved_.program);
  }

  inline auto Check(const KuraRect& rect) const -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    return IsValid(rect) ? KURA_RENDERER_OK : KURA_RENDERER_ERR_INVALID_ARG;
  }

  inline auto Check(const KuraRect& rect, const KuraColor& color) const -> Status {
    if (const auto status = Check(rect); status != KURA_RENDERER_OK)
      return status;
    return IsValid(color) ? KURA_RENDERER_OK : KURA_RENDERER_ERR_INVALID_ARG;
  }

  // The pixels `rect` touches, clamped so that huge rects can't overflow an int.
  static auto CoveredPixels(const KuraRect& rect) -> PixelRect {
    const auto clamp = [](const double v) {
      return static_cast<int>(std::clamp(v, -1048576.0, 1048576.0));
    };
    const auto x = static_cast<double>(rect.x);
    const auto y = static_cast<double>(rect.y);
    return PixelRect{clamp(std::floor(x)), clamp(std::floor(y)), clamp(std::ceil(x + static_cast<double>(rect.width))),
                     clamp(std::ceil(y + static_cast<double>(rect.height)))};
  }

  inline auto CurrentClip() const -> PixelRect {
    return clips_.empty() ? PixelRect{0, 0, static_cast<int>(width_), static_cast<int>(height_)} : clips_.back();
  }

  // Draws what was batched under the old clip, and then switches to the current one.
  void ApplyClip() {
    Flush();
    if (clips_.empty()) {
      glDisable(GL_SCISSOR_TEST);
      return;
    }
    const auto& clip = clips_.back();
    glEnable(GL_SCISSOR_TEST);
    // GL's origin is the bottom-left, Kura's is the top-left
    glScissor(clip.x0, static_cast<int>(height_) - clip.y1, clip.x1 - clip.x0, clip.y1 - clip.y0);
  }

  static inline auto Make(const float x, const float y, const KuraColor& color) -> Vertex {
    return Vertex{Coord(x), Coord(y), Channel(color.r), Channel(color.g), Channel(color.b), Channel(color.a)};
  }

  inline void AddRect(const float x, const float y, const float w, const float h, const KuraColor& color) {
    if (w <= 0.0f || h <= 0.0f)
      return;
    AddQuad(Make(x, y, color), Make(x + w, y, color), Make(x + w, y + h, color), Make(x, y + h, color));
  }

  void AddQuad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    if (verts_.size() + 4 > kMaxVertices || idx_.size() + 6 > kMaxIndices)
      Flush();
    const auto base = static_cast<uint32_t>(verts_.size());
    verts_.insert(verts_.end(), {a, b, c, d});
    idx_.insert(idx_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }

  void Flush() {
    if (idx_.empty())
      return;
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // handing GL a fresh store each time lets the driver keep drawing from the old one
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx_.size() * sizeof(uint32_t)), idx_.data(),
                 GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx_.size()), GL_UNSIGNED_INT, nullptr);
    verts_.clear();
    idx_.clear();
  }
};

namespace {
template <typename F>
inline auto Guard(F&& body) -> Status {
  try {
    return body();
  } catch (...) {
    return KURA_RENDERER_ERR_OUT_OF_RESOURCES;
  }
}

inline auto Self(void* self) -> GlImpl* {
  return static_cast<GlImpl*>(self);
}

auto OpBeginFrame(void* self, const uint32_t width, const uint32_t height) -> Status {
  return Guard([&] {
    return Self(self)->BeginFrame(width, height);
  });
}

auto OpEndFrame(void* self) -> Status {
  return Guard([&] {
    return Self(self)->EndFrame();
  });
}

auto OpPushClip(void* self, const KuraRect rect) -> Status {
  return Guard([&] {
    return Self(self)->PushClip(rect);
  });
}

auto OpPopClip(void* self) -> Status {
  return Guard([&] {
    return Self(self)->PopClip();
  });
}

auto OpFillRect(void* self, const KuraRect rect, const KuraColor color) -> Status {
  return Guard([&] {
    return Self(self)->FillRect(rect, color);
  });
}

auto OpStrokeRect(void* self, const KuraRect rect, const KuraColor color) -> Status {
  return Guard([&] {
    return Self(self)->StrokeRect(rect, color);
  });
}

auto OpDrawLine(void* self, const KuraVec2 from, const KuraVec2 to, const KuraColor color) -> Status {
  return Guard([&] {
    return Self(self)->DrawLine(from, to, color);
  });
}

auto OpDrawText(void* self, const KuraVec2 pos, const char* utf8, const size_t length, const KuraTextStyle style,
                const KuraColor color) -> Status {
  return Guard([&] {
    return Self(self)->DrawText(pos, utf8, length, style, color);
  });
}

auto OpMeasureText(void* self, const char* utf8, const size_t length, const KuraTextStyle style, KuraTextMetrics* out)
    -> Status {
  return Guard([&] {
    return Self(self)->MeasureText(utf8, length, style, out);
  });
}

void OpDestroy(void* self) {
  delete Self(self);
}

constexpr auto MakeOps() -> KuraRendererOps {
  KuraRendererOps ops{};
  ops.struct_size = sizeof(KuraRendererOps);
  ops.abi_version = KURA_RENDERER_ABI_VERSION;
  ops.destroy = &OpDestroy;
  ops.begin_frame = &OpBeginFrame;
  ops.end_frame = &OpEndFrame;
  ops.push_clip = &OpPushClip;
  ops.pop_clip = &OpPopClip;
  ops.fill_rect = &OpFillRect;
  ops.stroke_rect = &OpStrokeRect;
  ops.draw_line = &OpDrawLine;
  ops.draw_text = &OpDrawText;
  ops.measure_text = &OpMeasureText;
  return ops;
}

constexpr KuraRendererOps kOps = MakeOps();
}  // namespace
}  // namespace kura::renderer

extern "C" {
KuraRendererStatus kura_gl_renderer_create(const KuraGlOptions* options, KuraRenderer* renderer) {
  using namespace kura::renderer;
  if (renderer == nullptr)
    return KURA_RENDERER_ERR_INVALID_ARG;
  if (options != nullptr && options->struct_size < sizeof(KuraGlOptions))
    return KURA_RENDERER_ERR_INVALID_ARG;
  // without a context there's nothing to ask the version of, and calling GL would be a crash
  if (epoxy_gl_version() < 33)
    return KURA_RENDERER_ERR_INVALID_ARG;

  KuraGlOptions resolved{sizeof(KuraGlOptions), 1, {0.0f, 0.0f, 0.0f, 1.0f}};
  if (options != nullptr)
    resolved = *options;
  try {
    *renderer = KuraRenderer{&kOps, new GlImpl(resolved)};
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return KURA_RENDERER_ERR_OUT_OF_RESOURCES;
  } catch (...) {
    return KURA_RENDERER_ERR_OUT_OF_RESOURCES;
  }
  return KURA_RENDERER_OK;
}
}
